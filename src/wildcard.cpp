/** \file wildcard.c

Fish needs it's own globbing implementation to support
tab-expansion of globbed parameters. Also provides recursive
wildcards using **.

*/

#include "config.h" // IWYU pragma: keep
#include <stdlib.h>
#include <wchar.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <set>
#include <assert.h>
#include <stddef.h>
#include <wctype.h>
#include <string>
#include <utility>

#include "fallback.h"
#include "wutil.h"
#include "common.h"
#include "wildcard.h"
#include "complete.h"
#include "reader.h"
#include "expand.h"
#include <map>

/**
   Description for generic executable
*/
#define COMPLETE_EXEC_DESC _( L"Executable" )
/**
   Description for link to executable
*/
#define COMPLETE_EXEC_LINK_DESC _( L"Executable link" )

/**
   Description for regular file
*/
#define COMPLETE_FILE_DESC _( L"File" )
/**
   Description for character device
*/
#define COMPLETE_CHAR_DESC _( L"Character device" )
/**
   Description for block device
*/
#define COMPLETE_BLOCK_DESC _( L"Block device" )
/**
   Description for fifo buffer
*/
#define COMPLETE_FIFO_DESC _( L"Fifo" )
/**
   Description for symlink
*/
#define COMPLETE_SYMLINK_DESC _( L"Symbolic link" )
/**
   Description for symlink
*/
#define COMPLETE_DIRECTORY_SYMLINK_DESC _( L"Symbolic link to directory" )
/**
   Description for Rotten symlink
*/
#define COMPLETE_ROTTEN_SYMLINK_DESC _( L"Rotten symbolic link" )
/**
   Description for symlink loop
*/
#define COMPLETE_LOOP_SYMLINK_DESC _( L"Symbolic link loop" )
/**
   Description for socket files
*/
#define COMPLETE_SOCKET_DESC _( L"Socket" )
/**
   Description for directories
*/
#define COMPLETE_DIRECTORY_DESC _( L"Directory" )

/* Finds an internal (ANY_STRING, etc.) style wildcard, or wcstring::npos */
static size_t wildcard_find(const wchar_t *wc)
{
    for (size_t i=0; wc[i] != L'\0'; i++)
    {
        if (wc[i] == ANY_CHAR || wc[i] == ANY_STRING || wc[i] == ANY_STRING_RECURSIVE)
        {
            return i;
        }
    }
    return wcstring::npos;
}

// Implementation of wildcard_has. Needs to take the length to handle embedded nulls (#1631)
static bool wildcard_has_impl(const wchar_t *str, size_t len, bool internal)
{
    assert(str != NULL);
    const wchar_t *end = str + len;
    if (internal)
    {
        for (; str < end; str++)
        {
            if ((*str == ANY_CHAR) || (*str == ANY_STRING) || (*str == ANY_STRING_RECURSIVE))
                return true;
        }
    }
    else
    {
        wchar_t prev=0;
        for (; str < end; str++)
        {
            if (((*str == L'*') || (*str == L'?')) && (prev != L'\\'))
                return true;
            prev = *str;
        }
    }

    return false;
}

bool wildcard_has(const wchar_t *str, bool internal)
{
    assert(str != NULL);
    return wildcard_has_impl(str, wcslen(str), internal);
}

bool wildcard_has(const wcstring &str, bool internal)
{
    return wildcard_has_impl(str.data(), str.size(), internal);
}


/**
   Check whether the string str matches the wildcard string wc.

   \param str String to be matched.
   \param wc The wildcard.
   \param is_first Whether files beginning with dots should not be matched against wildcards.
*/
static bool wildcard_match_internal(const wchar_t *str, const wchar_t *wc, bool leading_dots_fail_to_match, bool is_first)
{
    if (*str == 0 && *wc==0)
    {
        /* We're done */
        return true;
    }

    /* Hackish fix for https://github.com/fish-shell/fish-shell/issues/270 . Prevent wildcards from matching . or .., but we must still allow literal matches. */
    if (leading_dots_fail_to_match && is_first && contains(str, L".", L".."))
    {
        /* The string is '.' or '..'. Return true if the wildcard exactly matches. */
        return ! wcscmp(str, wc);
    }

    if (*wc == ANY_STRING || *wc == ANY_STRING_RECURSIVE)
    {
        /* Ignore hidden file */
        if (leading_dots_fail_to_match && is_first && *str == L'.')
        {
            return false;
        }
        
        /* Common case of * at the end. In that case we can early out since we know it will match. */
        if (wc[1] == L'\0')
        {
            return true;
        }

        /* Try all submatches */
        do
        {
            if (wildcard_match_internal(str, wc+1, leading_dots_fail_to_match, false))
                return true;
        }
        while (*(str++) != 0);
        return false;
    }
    else if (*str == 0)
    {
        /*
          End of string, but not end of wildcard, and the next wildcard
          element is not a '*', so this is not a match.
        */
        return false;
    }
    else if (*wc == ANY_CHAR)
    {
        if (is_first && *str == L'.')
        {
            return false;
        }

        return wildcard_match_internal(str+1, wc+1, leading_dots_fail_to_match, false);
    }
    else if (*wc == *str)
    {
        return wildcard_match_internal(str+1, wc+1, leading_dots_fail_to_match, false);
    }

    return false;
}


/* This does something horrible refactored from an even more horrible function */
static wcstring resolve_description(wcstring *completion, const wchar_t *explicit_desc, wcstring(*desc_func)(const wcstring &))
{
    size_t complete_sep_loc = completion->find(PROG_COMPLETE_SEP);
    if (complete_sep_loc != wcstring::npos)
    {
        /* This completion has an embedded description, do not use the generic description */
        const wcstring description = completion->substr(complete_sep_loc + 1);
        completion->resize(complete_sep_loc);
        return description;
    }
    else
    {
        const wcstring func_result = (desc_func ? desc_func(*completion) : wcstring());
        if (! func_result.empty())
        {
            return func_result;
        }
        else
        {
            return explicit_desc ? explicit_desc : L"";
        }
    }
}

/* A transient parameter pack needed by wildcard_complete.f */
struct wc_complete_pack_t
{
    const wcstring &orig; // the original string, transient
    const wchar_t *desc; // literal description
    wcstring(*desc_func)(const wcstring &); // function for generating descriptions
    expand_flags_t expand_flags;
    wc_complete_pack_t(const wcstring &str) : orig(str) {}
};

/**
 Matches the string against the wildcard, and if the wildcard is a
 possible completion of the string, the remainder of the string is
 inserted into the out vector.
 
 We ignore ANY_STRING_RECURSIVE here. The consequence is that you cannot
 tab complete ** wildcards. This is historic behavior.
 */
static bool wildcard_complete_internal(const wchar_t *str,
                                       const wchar_t *wc,
                                       const wc_complete_pack_t &params,
                                       complete_flags_t flags,
                                       std::vector<completion_t> *out,
                                       bool is_first_call = false)
{
    assert(str != NULL);
    assert(wc != NULL);
    
    /* Maybe early out for hidden files. We require that the wildcard match these exactly (i.e. a dot); ANY_STRING not allowed */
    if (is_first_call && str[0] == L'.' && wc[0] != L'.')
    {
        return false;
    }
    
    /* Locate the next wildcard character position, e.g. ANY_CHAR or ANY_STRING */
    size_t next_wc_char_pos = wildcard_find(wc);
    
    /* Maybe we have no more wildcards at all. */
    if (next_wc_char_pos == wcstring::npos)
    {
        string_fuzzy_match_t match = string_fuzzy_match_string(wc, str);
        
        /* If we're allowing fuzzy match, any match is OK. Otherwise we require a prefix match. */
        bool match_acceptable;
        if (params.expand_flags & EXPAND_FUZZY_MATCH)
        {
            match_acceptable = match.type != fuzzy_match_none;
        }
        else
        {
            match_acceptable = match_type_shares_prefix(match.type);
        }
        
        if (match_acceptable)
        {
            /* Wildcard complete */
            bool full_replacement = match_type_requires_full_replacement(match.type) || (flags & COMPLETE_REPLACES_TOKEN);
            
            /* If we are not replacing the token, be careful to only store the part of the string after the wildcard */
            assert(!full_replacement || wcslen(wc) <= wcslen(str));
            wcstring out_completion = full_replacement ? params.orig : str + wcslen(wc);
            wcstring out_desc = resolve_description(&out_completion, params.desc, params.desc_func);
            
            /* Note: out_completion may be empty if the completion really is empty, e.g. tab-completing 'foo' when a file 'foo' exists. */
            complete_flags_t local_flags = flags | (full_replacement ? COMPLETE_REPLACES_TOKEN : 0);
            append_completion(out, out_completion, out_desc, local_flags, match);
        }
        return match_acceptable;
    }
    else if (next_wc_char_pos > 0)
    {
        bool result;
        /* Here we have a non-wildcard prefix. Note that we don't do fuzzy matching for stuff before a wildcard, so just do case comparison and then recurse. */
        if (wcsncmp(str, wc, next_wc_char_pos) == 0)
        {
            // Normal match
            result = wildcard_complete_internal(str + next_wc_char_pos, wc + next_wc_char_pos, params, flags, out);
        }
        else if (wcsncasecmp(str, wc, next_wc_char_pos) == 0)
        {
            // Case insensitive match
            result = wildcard_complete_internal(str + next_wc_char_pos, wc + next_wc_char_pos, params, flags | COMPLETE_REPLACES_TOKEN, out);
        }
        else
        {
            // No match
            result = false;
        }
        return result;
    }
    else
    {
        /* Our first character is a wildcard. */
        assert(next_wc_char_pos == 0);
        switch (wc[0])
        {
            case ANY_CHAR:
            {
                if (str[0] == L'\0')
                {
                    return false;
                }
                else
                {
                    return wildcard_complete_internal(str + 1, wc + 1, params, flags, out);
                }
                break;
            }
                
            case ANY_STRING:
            {
                bool has_match = false;
                /* Try all submatches. #929: if the recursive call gives us a prefix match, just stop. This is sloppy - what we really want to do is say, once we've seen a match of a particular type, ignore all matches of that type further down the string, such that the wildcard produces the "minimal match." */
                bool has_prefix_match = false;
                for (size_t i=0; str[i] != L'\0' && ! has_prefix_match; i++)
                {
                    const size_t before_count = out->size();
                    if (wildcard_complete_internal(str + i, wc + 1, params, flags, out))
                    {
                        has_match = true;
                        
                        /* Determine if we have a prefix match */
                        const size_t after_count = out->size();
                        for (size_t j = before_count; j < after_count; j++)
                        {
                            if (out->at(j).match.type <= fuzzy_match_prefix)
                            {
                                has_prefix_match = true;
                                break;
                            }
                        }
                    }
                }
                return has_match;
            }
                
            case ANY_STRING_RECURSIVE:
                /* We don't even try with this one */
                return false;
            
            default:
                assert(0 && "Unreachable code reached");
                return false;
        }
    }
    assert(0 && "Unreachable code reached");
}

bool wildcard_complete(const wcstring &str,
                       const wchar_t *wc,
                       const wchar_t *desc,
                       wcstring(*desc_func)(const wcstring &),
                       std::vector<completion_t> *out,
                       expand_flags_t expand_flags,
                       complete_flags_t flags)
{
    assert(out != NULL);
    wc_complete_pack_t params(str);
    params.desc = desc;
    params.desc_func = desc_func;
    params.expand_flags = expand_flags;
    return wildcard_complete_internal(str.c_str(), wc, params, flags, out, true /* first call */);
}


bool wildcard_match(const wcstring &str, const wcstring &wc, bool leading_dots_fail_to_match)
{
    return wildcard_match_internal(str.c_str(), wc.c_str(), leading_dots_fail_to_match, true /* first */);
}

/**
   Creates a path from the specified directory and filename.
*/
static wcstring make_path(const wcstring &base_dir, const wcstring &name)
{
    return base_dir + name;
}

/**
   Obtain a description string for the file specified by the filename.

   The returned value is a string constant and should not be free'd.

   \param filename The file for which to find a description string
   \param lstat_res The result of calling lstat on the file
   \param lbuf The struct buf output of calling lstat on the file
   \param stat_res The result of calling stat on the file
   \param buf The struct buf output of calling stat on the file
   \param err The errno value after a failed stat call on the file.
*/

static wcstring file_get_desc(const wcstring &filename,
                              int lstat_res,
                              const struct stat &lbuf,
                              int stat_res,
                              struct stat buf,
                              int err)
{

    if (!lstat_res)
    {
        if (S_ISLNK(lbuf.st_mode))
        {
            if (!stat_res)
            {
                if (S_ISDIR(buf.st_mode))
                {
                    return COMPLETE_DIRECTORY_SYMLINK_DESC;
                }
                else
                {

                    if ((buf.st_mode & S_IXUSR) ||
                            (buf.st_mode & S_IXGRP) ||
                            (buf.st_mode & S_IXOTH))
                    {

                        if (waccess(filename, X_OK) == 0)
                        {
                            /*
                              Weird group permissions and other such
                              issues make it non-trivial to find out
                              if we can actually execute a file using
                              the result from stat. It is much safer
                              to use the access function, since it
                              tells us exactly what we want to know.
                            */
                            return COMPLETE_EXEC_LINK_DESC;
                        }
                    }
                }

                return COMPLETE_SYMLINK_DESC;

            }
            else
            {
                switch (err)
                {
                    case ENOENT:
                    {
                        return COMPLETE_ROTTEN_SYMLINK_DESC;
                    }

                    case ELOOP:
                    {
                        return COMPLETE_LOOP_SYMLINK_DESC;
                    }
                }
                /*
                  On unknown errors we do nothing. The file will be
                  given the default 'File' description or one based on the suffix.
                */
            }

        }
        else if (S_ISCHR(buf.st_mode))
        {
            return COMPLETE_CHAR_DESC;
        }
        else if (S_ISBLK(buf.st_mode))
        {
            return COMPLETE_BLOCK_DESC;
        }
        else if (S_ISFIFO(buf.st_mode))
        {
            return COMPLETE_FIFO_DESC;
        }
        else if (S_ISSOCK(buf.st_mode))
        {
            return COMPLETE_SOCKET_DESC;
        }
        else if (S_ISDIR(buf.st_mode))
        {
            return COMPLETE_DIRECTORY_DESC;
        }
        else
        {
            if ((buf.st_mode & S_IXUSR) ||
                    (buf.st_mode & S_IXGRP) ||
                    (buf.st_mode & S_IXOTH))
            {

                if (waccess(filename, X_OK) == 0)
                {
                    /*
                      Weird group permissions and other such issues
                      make it non-trivial to find out if we can
                      actually execute a file using the result from
                      stat. It is much safer to use the access
                      function, since it tells us exactly what we want
                      to know.
                    */
                    return COMPLETE_EXEC_DESC;
                }
            }
        }
    }

    return COMPLETE_FILE_DESC ;
}


/**
   Add the specified filename if it matches the specified wildcard.

   If the filename matches, first get the description of the specified
   filename. If this is a regular file, append the filesize to the
   description.

   \param list the list to add he completion to
   \param fullname the full filename of the file
   \param completion the completion part of the file name
   \param wc the wildcard to match against
   \param is_cmd whether we are performing command completion
*/
static void wildcard_completion_allocate(std::vector<completion_t> *list,
        const wcstring &fullname,
        const wcstring &completion,
        const wchar_t *wc,
        expand_flags_t expand_flags)
{
    assert(list != NULL);
    struct stat buf, lbuf;
    wcstring sb;
    wcstring munged_completion;

    int flags = 0;
    int stat_res, lstat_res;
    int stat_errno=0;

    long long sz;

    /*
      If the file is a symlink, we need to stat both the file itself
      _and_ the destination file. But we try to avoid this with
      non-symlinks by first doing an lstat, and if the file is not a
      link we copy the results over to the regular stat buffer.
    */
    if ((lstat_res = lwstat(fullname, &lbuf)))
    {
        /* lstat failed! */
        sz=-1;
        stat_res = lstat_res;
    }
    else
    {
        if (S_ISLNK(lbuf.st_mode))
        {

            if ((stat_res = wstat(fullname, &buf)))
            {
                sz=-1;
            }
            else
            {
                sz = (long long)buf.st_size;
            }

            /*
              In order to differentiate between e.g. rotten symlinks
              and symlink loops, we also need to know the error status of wstat.
            */
            stat_errno = errno;
        }
        else
        {
            stat_res = lstat_res;
            memcpy(&buf, &lbuf, sizeof(struct stat));
            sz = (long long)buf.st_size;
        }
    }


    bool wants_desc = !(expand_flags & EXPAND_NO_DESCRIPTIONS);
    wcstring desc;
    if (wants_desc)
        desc = file_get_desc(fullname, lstat_res, lbuf, stat_res, buf, stat_errno);

    if (sz >= 0 && S_ISDIR(buf.st_mode))
    {
        flags |= COMPLETE_NO_SPACE;
        munged_completion = completion;
        munged_completion.push_back(L'/');
        if (wants_desc)
            sb.append(desc);
    }
    else
    {
        if (wants_desc)
        {
            if (! desc.empty())
            {
                sb.append(desc);
                sb.append(L", ");
            }
            sb.append(format_size(sz));
        }
    }

    const wcstring &completion_to_use = munged_completion.empty() ? completion : munged_completion;
    wildcard_complete(completion_to_use, wc, sb.c_str(), NULL, list, expand_flags, flags);
}

/**
  Test if the file specified by the given filename matches the
  expansion flags specified. flags can be a combination of
  EXECUTABLES_ONLY and DIRECTORIES_ONLY.
*/
static bool test_flags(const wcstring &filename, expand_flags_t flags)
{
    if (flags & DIRECTORIES_ONLY)
    {
        struct stat buf;
        if (wstat(filename, &buf) == -1)
        {
            return false;
        }

        if (!S_ISDIR(buf.st_mode))
        {
            return false;
        }
    }


    if (flags & EXECUTABLES_ONLY)
    {
        if (waccess(filename, X_OK) != 0)
            return false;
        struct stat buf;
        if (wstat(filename, &buf) == -1)
        {
            return false;
        }

        if (!S_ISREG(buf.st_mode))
        {
            return false;
        }
    }

    return true;
}

/** Appends a completion to the completion list, if the string is missing from the set. */
static void insert_completion_if_missing(const wcstring &str, std::vector<completion_t> *out, std::set<wcstring> *completion_set)
{
    if (completion_set->insert(str).second)
        append_completion(out, str);
}

class wildcard_expander_t
{
    /* the set of items we have resolved, used to avoid duplication */
    std::set<wcstring> completion_set;
    
    /* the set of file IDs we have visited, used to avoid symlink loops */
    std::set<file_id_t> visited_files;
    
    /* flags controlling expansion */
    const expand_flags_t flags;
    
    /* resolved items get inserted into here. This is transient of course. */
    std::vector<completion_t> *resolved;
    
    /* whether we have been interrupted */
    bool did_interrupt;
    
    /* whether we have successfully added any completions */
    bool did_add;
    
    /* We are a trailing slash - expand at the end */
    void expand_trailing_slash(const wcstring &base_dir);
    
    /* Given a directory base_dir, which is opened as base_dir_fp, expand an intermediate segment of the wildcard.
       Treat ANY_STRING_RECURSIVE as ANY_STRING.
       wc_segment is the wildcard segment for this directory
       wc_remainder is the wildcard for subdirectories
     */
    void expand_intermediate_segment(const wcstring &base_dir, DIR *base_dir_fp, const wcstring &wc_segment, const wchar_t *wc_remainder);
    
    /* Given a directory base_dir, which is opened as base_dir_fp, expand the last segment of the wildcard.
     Treat ANY_STRING_RECURSIVE as ANY_STRING.
     wc is the wildcard segment to use for matching
     wc_remainder is the wildcard for subdirectories
     */
    void expand_last_segment(const wcstring &base_dir, DIR *base_dir_fp, const wcstring &wc);
    
    /* Indicate whether we should cancel wildcard expansion. This latches 'interrupt' */
    bool interrupted()
    {
        if (! did_interrupt)
        {
            did_interrupt = (is_main_thread() ? reader_interrupted() : reader_thread_job_is_stale());
        }
        return did_interrupt;
    }
    
    /* Helper to resolve an empty base directory */
    static DIR *open_dir(const wcstring &base_dir)
    {
        return wopendir(base_dir.empty() ? L"." : base_dir);
    }
    
public:
    
    wildcard_expander_t(expand_flags_t f, std::vector<completion_t> *r) : flags(f), resolved(r), did_interrupt(false), did_add(false)
    {
        assert(resolved != NULL);
        
        /* Insert initial completions into our set to avoid duplicates */
        for (std::vector<completion_t>::const_iterator iter = resolved->begin(); iter != resolved->end(); ++iter)
        {
            this->completion_set.insert(iter->completion);
        }
    }
    
    /* Do wildcard expansion. This is recursive. */
    void expand(const wcstring &base_dir, const wchar_t *wc);
    
    int status_code() const
    {
        if (this->did_interrupt)
        {
            return -1;
        }
        else
        {
            return this->did_add ? 1 : 0;
        }
    }
};

void wildcard_expander_t::expand_trailing_slash(const wcstring &base_dir)
{
    if (interrupted())
    {
        return;
    }
    
    if (! (flags & FOR_COMPLETIONS))
    {
        /* Trailing slash and not accepting incomplete, e.g. `echo /tmp/`. Insert this file if it exists. */
        if (waccess(base_dir, F_OK))
        {
            append_completion(this->resolved, base_dir);
            this->did_add = true;
        }
    }
    else
    {
        /* Trailing slashes and accepting incomplete, e.g. `echo /tmp/<tab>`. Everything is added. */
        DIR *dir = open_dir(base_dir);
        if (dir)
        {
            wcstring next;
            while (wreaddir(dir, next) && ! interrupted())
            {
                if (! next.empty() && next.at(0) != L'.')
                {
                    const wcstring abs_path = base_dir + next;
                    if (test_flags(abs_path, this->flags))
                    {
                        wildcard_completion_allocate(this->resolved, abs_path, next, L"", flags);
                        this->did_add = true;
                    }
                }
            }
            closedir(dir);
        }
    }
}

void wildcard_expander_t::expand_intermediate_segment(const wcstring &base_dir, DIR *base_dir_fp, const wcstring &wc_segment, const wchar_t *wc_remainder)
{
    wcstring name_str;
    while (!interrupted() && wreaddir(base_dir_fp, name_str))
    {
        /* Note that it's critical we ignore leading dots here, else we may descend into . and .. */
        if (! wildcard_match(name_str, wc_segment, true))
        {
            /* Doesn't match the wildcard for this segment, skip it */
            continue;
        }
        
        wcstring full_path = base_dir + name_str;
        struct stat buf;
        if (0 != wstat(full_path, &buf) || !S_ISDIR(buf.st_mode))
        {
            /* We either can't stat it, or we did but it's not a directory */
            continue;
        }

        const file_id_t file_id = file_id_t::file_id_from_stat(&buf);
        if (!this->visited_files.insert(file_id).second)
        {
            /* Symlink loop! This directory was already visited, so skip it */
            continue;
        }

        /* We made it through. Perform normal wildcard expansion on this new directory, starting at our tail_wc, which includes the ANY_STRING_RECURSIVE guy. */
        full_path.push_back(L'/');
        this->expand(full_path, wc_remainder);
    }
}

void wildcard_expander_t::expand_last_segment(const wcstring &base_dir, DIR *base_dir_fp, const wcstring &wc)
{
    wcstring name_str;
    while (wreaddir(base_dir_fp, name_str))
    {
        if (flags & FOR_COMPLETIONS)
        {
            /* Test for matches before stating file, so as to minimize the number of calls to the much slower stat function. The only expand flag we care about is EXPAND_FUZZY_MATCH; we have no complete flags. */
            std::vector<completion_t> local_matches;
            if (wildcard_complete(name_str, wc.c_str(), L"", NULL, &local_matches, flags & EXPAND_FUZZY_MATCH, 0))
            {
                const wcstring abs_path = base_dir + name_str;
                if (test_flags(abs_path, flags))
                {
                    wildcard_completion_allocate(this->resolved, abs_path, name_str, wc.c_str(), flags);
                    this->did_add = true;
                }
            }
        }
        else
        {
            // Normal wildcard expansion, not for completions
            if (wildcard_match(name_str, wc, true /* skip files with leading dots */))
            {
                const wcstring abs_path = base_dir + name_str;
                if (this->completion_set.insert(abs_path).second)
                {
                    append_completion(this->resolved, abs_path);
                    this->did_add = true;
                }
            }
        }
    }
}

/**
 The real implementation of wildcard expansion is in this
 function. Other functions are just wrappers around this one.
 
 This function traverses the relevant directory tree looking for
 matches, and recurses when needed to handle wildcrards spanning
 multiple components and recursive wildcards.
 
 Because this function calls itself recursively with substrings,
 it's important that the parameters be raw pointers instead of wcstring,
 which would be too expensive to construct for all substrings.
 
 Args:
 base_dir: the "working directory" against which the wildcard is to be resolved
 wc: the wildcard string itself, e.g. foo*bar/baz (where * is acutally ANY_CHAR)
*/
void wildcard_expander_t::expand(const wcstring &base_dir, const wchar_t *wc)
{
    assert(wc != NULL);
    
    if (interrupted())
    {
        return;
    }
    
    /* Get the current segment and compute interesting properties about it. */
    const size_t wc_len = wcslen(wc);
    const wchar_t * const next_slash = wcschr(wc, L'/');
    const bool is_last_segment = (next_slash == NULL);
    const size_t wc_segment_len = next_slash ? next_slash - wc : wc_len;
    const wcstring wc_segment = wcstring(wc, wc_segment_len);
    const bool segment_has_wildcards = wildcard_has(wc_segment, true /* internal, i.e. look for ANY_CHAR instead of ? */);
    
    if (wc_segment.empty())
    {
        /* Handle empty segment */
        assert(! segment_has_wildcards);
        if (is_last_segment)
        {
            this->expand_trailing_slash(base_dir);
        }
        else
        {
            /* Multiple adjacent slashes in the wildcard. Just skip them. */
            this->expand(base_dir, next_slash + 1);
        }
    }
    else if (! segment_has_wildcards && ! is_last_segment)
    {
        /* Literal intermediate match. Note that we may not be able to actually read the directory (#2099) */
        assert(next_slash != NULL);
        /* This just trumps everything */
        this->expand(base_dir + wc_segment + L'/', next_slash + 1);
    }
    else
    {
        assert(! wc_segment.empty() && (segment_has_wildcards || is_last_segment));
        DIR *dir = open_dir(base_dir);
        if (dir)
        {
            if (is_last_segment)
            {
                /* Last wildcard segment, nonempty wildcard */
                this->expand_last_segment(base_dir, dir, wc_segment);
            }
            else
            {
                /* Not the last segment, nonempty wildcard */
                assert(next_slash != NULL);
                const wchar_t *wc_remainder = next_slash;
                while (*wc_remainder == L'/')
                {
                    wc_remainder++;
                }
                this->expand_intermediate_segment(base_dir, dir, wc_segment, wc_remainder);
            }
            
            /* Recursive wildcards require special handling */
            size_t asr_idx = wc_segment.find(ANY_STRING_RECURSIVE);
            if (asr_idx != wcstring::npos)
            {
                /* Construct a "head + any" wildcard for matching stuff in this directory, and an "any + tail" wildcard for matching stuff in subdirectories. Note that the ANY_STRING_RECURSIVE character is present in both the head and the tail. */
                const wcstring head_any(wc_segment, 0, asr_idx + 1);
                const wchar_t *any_tail = wc + asr_idx;
                assert(head_any.at(head_any.size() - 1) == ANY_STRING_RECURSIVE);
                assert(any_tail[0] == ANY_STRING_RECURSIVE);

                rewinddir(dir);
                this->expand_intermediate_segment(base_dir, dir, head_any, any_tail);
            }
            closedir(dir);
        }
    }
}

/**
   The real implementation of wildcard expansion is in this
   function. Other functions are just wrappers around this one.

   This function traverses the relevant directory tree looking for
   matches, and recurses when needed to handle wildcrards spanning
   multiple components and recursive wildcards.

   Because this function calls itself recursively with substrings,
   it's important that the parameters be raw pointers instead of wcstring,
   which would be too expensive to construct for all substrings.
 
   Args:
     wc: the wildcard string itself, e.g. foo* (where * is acutally ANY_CHAR)
     base_dir: the "working directory" against which the wildcard is to be resolved
     flags: flags controlling expansion
     out: resolved items get inserted into here
     completion_set: the set of items we have resolved, used to avoid duplication
     visited_files: the set of file IDs we have visited, used to avoid symlink loops
 
 Returns:
   1 if matches were found, 0 if not, -1 on abort (control-C)
 */
static int wildcard_expand_internal(const wchar_t *wc,
                                    const wchar_t * const base_dir,
                                    expand_flags_t flags,
                                    std::vector<completion_t> *out,
                                    std::set<wcstring> &completion_set,
                                    std::set<file_id_t> &visited_files)
{
    /* Variables for traversing a directory */
    DIR *dir;

    /* The result returned */
    int res = 0;


    //  debug( 3, L"WILDCARD_EXPAND %ls in %ls", wc, base_dir );

    if (is_main_thread() ? reader_interrupted() : reader_thread_job_is_stale())
    {
        return -1;
    }

    if (!wc || !base_dir)
    {
        debug(2, L"Got null string on line %d of file %s", __LINE__, __FILE__);
        return 0;
    }

    const size_t base_dir_len = wcslen(base_dir);
    const size_t wc_len = wcslen(wc);

    if (flags & FOR_COMPLETIONS)
    {
        /*
           Avoid excessive number of returned matches for wc ending with a *
        */
        if (wc_len > 0 && (wc[wc_len-1]==ANY_STRING))
        {
            wchar_t * foo = wcsdup(wc);
            foo[wc_len-1]=0;
            int res = wildcard_expand_internal(foo, base_dir, flags, out, completion_set, visited_files);
            free(foo);
            return res;
        }
    }

    /* Determine if we are the last segment */
    const wchar_t * const next_slash = wcschr(wc,L'/');
    const bool is_last_segment = (next_slash == NULL);
    const size_t wc_segment_len = next_slash ? next_slash - wc : wc_len;
    const wcstring wc_segment = wcstring(wc, wc_segment_len);
    
    /* Maybe this segment has no wildcards at all. If this is not the last segment, and it has no wildcards, then we don't need to match against the directory contents, and in fact we don't want to match since we may not be able to read it anyways (#2099). Don't even open the directory! */
    const bool segment_has_wildcards = wildcard_has(wc_segment, true /* internal, i.e. look for ANY_CHAR instead of ? */);
    if (! segment_has_wildcards && ! is_last_segment)
    {
        wcstring new_base_dir = make_path(base_dir, wc_segment);
        new_base_dir.push_back(L'/');
        
        /* Skip multiple separators */
        assert(next_slash != NULL);
        const wchar_t *new_wc = next_slash;
        while (*new_wc==L'/')
        {
            new_wc++;
        }
        /* Early out! */
        return wildcard_expand_internal(new_wc, new_base_dir.c_str(), flags, out, completion_set, visited_files);
    }
    
    /* Test for recursive match string in current segment */
    const bool is_recursive = (wc_segment.find(ANY_STRING_RECURSIVE) != wcstring::npos);
    

    const wchar_t *base_dir_or_cwd = (base_dir[0] == L'\0') ? L"." : base_dir;
    if (!(dir = wopendir(base_dir_or_cwd)))
    {
        return 0;
    }
    
    /*
      Is this segment of the wildcard the last?
    */
    if (is_last_segment)
    {
        /*
          Wildcard segment is the last segment,

          Insert all matching files/directories
        */
        if (wc[0]=='\0')
        {
            /*
              The last wildcard segment is empty. Insert everything if
              completing, the directory itself otherwise.
            */
            if (flags & FOR_COMPLETIONS)
            {
                wcstring next;
                while (wreaddir(dir, next))
                {
                    if (next[0] != L'.')
                    {
                        wcstring long_name = make_path(base_dir, next);

                        if (test_flags(long_name.c_str(), flags))
                        {
                            wildcard_completion_allocate(out, long_name, next, L"", flags);
                        }
                    }
                }
            }
            else
            {
                res = 1;
                insert_completion_if_missing(base_dir, out, &completion_set);
            }
        }
        else
        {
            /* This is the last wildcard segment, and it is not empty. Match files/directories. */
            wcstring name_str;
            while (wreaddir(dir, name_str))
            {
                if (flags & FOR_COMPLETIONS)
                {

                    const wcstring long_name = make_path(base_dir, name_str);

                    /* Test for matches before stating file, so as to minimize the number of calls to the much slower stat function. The only expand flag we care about is EXPAND_FUZZY_MATCH; we have no complete flags. */
                    std::vector<completion_t> test;
                    if (wildcard_complete(name_str, wc, L"", NULL, &test, flags & EXPAND_FUZZY_MATCH, 0))
                    {
                        if (test_flags(long_name.c_str(), flags))
                        {
                            wildcard_completion_allocate(out, long_name, name_str, wc, flags);

                        }
                    }
                }
                else
                {
                    if (wildcard_match(name_str, wc, true /* skip files with leading dots */))
                    {
                        const wcstring long_name = make_path(base_dir, name_str);
                        int skip = 0;

                        if (is_recursive)
                        {
                            /*
                              In recursive mode, we are only
                              interested in adding files -directories
                              will be added in the next pass.
                            */
                            struct stat buf;
                            if (!wstat(long_name, &buf))
                            {
                                skip = S_ISDIR(buf.st_mode);
                            }
                        }
                        if (! skip)
                        {
                            insert_completion_if_missing(long_name, out, &completion_set);
                        }
                        res = 1;
                    }
                }
            }
        }
    }

    if ((! is_last_segment) || is_recursive)
    {
        /*
          Wilcard segment is not the last segment.  Recursively call
          wildcard_expand for all matching subdirectories.
        */

        /*
          In recursive mode, we look through the directory twice. If
          so, this rewind is needed.
        */
        rewinddir(dir);

        /* new_dir is a scratch area containing the full path to a file/directory we are iterating over */
        wcstring new_dir = base_dir;

        wcstring name_str;
        while (wreaddir(dir, name_str))
        {
            /*
              Test if the file/directory name matches the whole
              wildcard element, i.e. regular matching.
            */
            bool whole_match = wildcard_match(name_str, wc_segment, true /* ignore leading dots */);
            bool partial_match = false;

            /*
               If we are doing recursive matching, also check if this
               directory matches the part up to the recusrive
               wildcard, if so, then we can search all subdirectories
               for matches.
            */
            if (is_recursive)
            {
                const wchar_t *end = wcschr(wc, ANY_STRING_RECURSIVE);
                wchar_t *wc_sub = wcsndup(wc, end-wc+1);
                partial_match = wildcard_match(name_str, wc_sub, true /* ignore leading dots */);
                free(wc_sub);
            }

            if (whole_match || partial_match)
            {
                struct stat buf;
                int new_res;

                // new_dir is base_dir + some other path components
                // Replace everything after base_dir with the new path component
                new_dir.replace(base_dir_len, wcstring::npos, name_str);

                int stat_res = wstat(new_dir, &buf);

                if (!stat_res)
                {
                    // Insert a "file ID" into visited_files
                    // If the insertion fails, we've already visited this file (i.e. a symlink loop)
                    // If we're not recursive, insert anyways (in case we loop back around in a future recursive segment), but continue on; the idea being that literal path components should still work
                    const file_id_t file_id = file_id_t::file_id_from_stat(&buf);
                    if (S_ISDIR(buf.st_mode) && (visited_files.insert(file_id).second || ! is_recursive))
                    {
                        new_dir.push_back(L'/');

                        /*
                          Regular matching
                        */
                        if (whole_match)
                        {
                            const wchar_t *new_wc = L"";
                            if (next_slash)
                            {
                                new_wc=next_slash+1;
                                /*
                                  Accept multiple '/' as a single directory separator
                                */
                                while (*new_wc==L'/')
                                {
                                    new_wc++;
                                }
                            }

                            new_res = wildcard_expand_internal(new_wc,
                                                               new_dir.c_str(),
                                                               flags,
                                                               out,
                                                               completion_set,
                                                               visited_files);

                            if (new_res == -1)
                            {
                                res = -1;
                                break;
                            }
                            res |= new_res;

                        }

                        /*
                          Recursive matching
                        */
                        if (partial_match)
                        {

                            new_res = wildcard_expand_internal(wcschr(wc, ANY_STRING_RECURSIVE),
                                                               new_dir.c_str(),
                                                               flags,
                                                               out,
                                                               completion_set,
                                                               visited_files);

                            if (new_res == -1)
                            {
                                res = -1;
                                break;
                            }
                            res |= new_res;

                        }
                    }
                }
            }
        }
    }
    closedir(dir);

    return res;
}


static int wildcard_expand(const wchar_t *wc,
                           const wcstring &base_dir,
                           expand_flags_t flags,
                           std::vector<completion_t> *out)
{
    assert(out != NULL);
    size_t c = out->size();
    
    wildcard_expander_t expander(flags, out);
    expander.expand(base_dir, wc);

    if (flags & FOR_COMPLETIONS)
    {
        wcstring wc_base;
        const wchar_t *wc_base_ptr = wcsrchr(wc, L'/');
        if (wc_base_ptr)
        {
            wc_base = wcstring(wc, (wc_base_ptr-wc)+1);
        }

        for (size_t i=c; i<out->size(); i++)
        {
            completion_t &c = out->at(i);
            
            // completion = base_dir + wc_base + completion
            c.prepend_token_prefix(wc_base);
            c.prepend_token_prefix(base_dir);
        }
    }
    return expander.status_code();
}

int wildcard_expand_string(const wcstring &wc, const wcstring &base_dir, expand_flags_t flags, std::vector<completion_t> *output)
{
    assert(output != NULL);
    /* Hackish fix for 1631. We are about to call c_str(), which will produce a string truncated at any embedded nulls. We could fix this by passing around the size, etc. However embedded nulls are never allowed in a filename, so we just check for them and return 0 (no matches) if there is an embedded null. */
    if (wc.find(L'\0') != wcstring::npos)
    {
        return 0;
    }
    return wildcard_expand(wc.c_str(), base_dir, flags, output);
}
