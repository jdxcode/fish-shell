
#
# Completions for the hg command
# This file was autogenerated by the file make_mercurial_completions.fish
# which is shipped with the fish source code
#

#
# Completions from commandline
#


#
# subcommands
#

complete -c hg -n '__fish_use_subcommand' -x -a add --description 'add the specified files on the next commit'
complete -c hg -n '__fish_use_subcommand' -x -a annotate --description 'show changeset information per file line'
complete -c hg -n '__fish_use_subcommand' -x -a archive --description 'create unversioned archive of a repository revision'
complete -c hg -n '__fish_use_subcommand' -x -a backout --description 'reverse effect of earlier changeset'
complete -c hg -n '__fish_use_subcommand' -x -a bundle --description 'create a changegroup file'
complete -c hg -n '__fish_use_subcommand' -x -a cat --description 'output the latest or given revisions of files'
complete -c hg -n '__fish_use_subcommand' -x -a clone --description 'make a copy of an existing repository'
complete -c hg -n '__fish_use_subcommand' -x -a commit --description 'commit the specified files or all outstanding changes'
complete -c hg -n '__fish_use_subcommand' -x -a copy --description 'mark files as copied for the next commit'
complete -c hg -n '__fish_use_subcommand' -x -a diff --description 'diff repository (or selected files)'
complete -c hg -n '__fish_use_subcommand' -x -a export --description 'dump the header and diffs for one or more changesets'
complete -c hg -n '__fish_use_subcommand' -x -a grep --description 'search for a pattern in specified files and revisions'
complete -c hg -n '__fish_use_subcommand' -x -a heads --description 'show current repository heads'
complete -c hg -n '__fish_use_subcommand' -x -a help --description 'show help for a command, extension, or list of commands'
complete -c hg -n '__fish_use_subcommand' -x -a identify --description 'print information about the working copy'
complete -c hg -n '__fish_use_subcommand' -x -a import --description 'import an ordered set of patches'
complete -c hg -n '__fish_use_subcommand' -x -a incoming --description 'show new changesets found in source'
complete -c hg -n '__fish_use_subcommand' -x -a init --description 'create a new repository in the given directory'
complete -c hg -n '__fish_use_subcommand' -x -a locate --description 'locate files matching specific patterns'
complete -c hg -n '__fish_use_subcommand' -x -a log --description 'show revision history of entire repository or files'
complete -c hg -n '__fish_use_subcommand' -x -a manifest --description 'output the latest or given revision of the project manifest'
complete -c hg -n '__fish_use_subcommand' -x -a merge --description 'Merge working directory with another revision'
complete -c hg -n '__fish_use_subcommand' -x -a outgoing --description 'show changesets not found in destination'
complete -c hg -n '__fish_use_subcommand' -x -a parents --description 'show the parents of the working dir or revision'
complete -c hg -n '__fish_use_subcommand' -x -a paths --description 'show definition of symbolic path names'
complete -c hg -n '__fish_use_subcommand' -x -a pull --description 'pull changes from the specified source'
complete -c hg -n '__fish_use_subcommand' -x -a push --description 'push changes to the specified destination'
complete -c hg -n '__fish_use_subcommand' -x -a recover --description 'roll back an interrupted transaction'
complete -c hg -n '__fish_use_subcommand' -x -a remove --description 'remove the specified files on the next commit'
complete -c hg -n '__fish_use_subcommand' -x -a rename --description 'rename files; equivalent of copy + remove'
complete -c hg -n '__fish_use_subcommand' -x -a revert --description 'revert files or dirs to their states as of some revision'
complete -c hg -n '__fish_use_subcommand' -x -a rollback --description 'roll back the last transaction in this repository'
complete -c hg -n '__fish_use_subcommand' -x -a root --description 'print the root (top) of the current working dir'
complete -c hg -n '__fish_use_subcommand' -x -a serve --description 'export the repository via HTTP'
complete -c hg -n '__fish_use_subcommand' -x -a status --description 'show changed files in the working directory'
complete -c hg -n '__fish_use_subcommand' -x -a tag --description 'add a tag for the current tip or a given revision'
complete -c hg -n '__fish_use_subcommand' -x -a tags --description 'list repository tags'
complete -c hg -n '__fish_use_subcommand' -x -a tip --description 'show the tip revision'
complete -c hg -n '__fish_use_subcommand' -x -a unbundle --description 'apply a changegroup file'
complete -c hg -n '__fish_use_subcommand' -x -a update --description 'update or merge working directory'
complete -c hg -n '__fish_use_subcommand' -x -a verify --description 'verify the integrity of the repository'
complete -c hg -n '__fish_use_subcommand' -x -a version --description 'output version and copyright information'


#
# Completions for the 'add' subcommand
#

complete -c hg -n 'contains \'add\' (commandline -poc)' -s I -l include --description 'Include names matching the given patterns'
complete -c hg -n 'contains \'add\' (commandline -poc)' -s X -l exclude --description 'Exclude names matching the given patterns'
complete -c hg -n 'contains \'add\' (commandline -poc)' -s n -l dry-run --description 'Do not perform actions, just print output'


#
# Completions for the 'annotate' subcommand
#

complete -c hg -n 'contains \'annotate\' (commandline -poc)' -s r -l rev --description 'Annotate the specified revision'
complete -c hg -n 'contains \'annotate\' (commandline -poc)' -s a -l text --description 'Treat all files as text'
complete -c hg -n 'contains \'annotate\' (commandline -poc)' -s u -l user --description 'List the author'
complete -c hg -n 'contains \'annotate\' (commandline -poc)' -s d -l date --description 'List the date'
complete -c hg -n 'contains \'annotate\' (commandline -poc)' -s n -l number --description 'List the revision number (default)'
complete -c hg -n 'contains \'annotate\' (commandline -poc)' -s c -l changeset --description 'List the changeset'
complete -c hg -n 'contains \'annotate\' (commandline -poc)' -s I -l include --description 'Include names matching the given patterns'
complete -c hg -n 'contains \'annotate\' (commandline -poc)' -s X -l exclude --description 'Exclude names matching the given patterns'


#
# Completions for the 'archive' subcommand
#

complete -c hg -n 'contains \'archive\' (commandline -poc)' -l no-decode --description 'Do not pass files through decoders'
complete -c hg -n 'contains \'archive\' (commandline -poc)' -s p -l prefix --description 'Directory prefix for files in archive'
complete -c hg -n 'contains \'archive\' (commandline -poc)' -s r -l rev --description 'Revision to distribute'
complete -c hg -n 'contains \'archive\' (commandline -poc)' -s t -l type --description 'Type of distribution to create'
complete -c hg -n 'contains \'archive\' (commandline -poc)' -s I -l include --description 'Include names matching the given patterns'
complete -c hg -n 'contains \'archive\' (commandline -poc)' -s X -l exclude --description 'Exclude names matching the given patterns'


#
# Completions for the 'backout' subcommand
#

complete -c hg -n 'contains \'backout\' (commandline -poc)' -l merge -x --description 'Remembers the parent of the working directory'
complete -c hg -n 'contains \'backout\' (commandline -poc)' -l merge --description 'Merge with old dirstate parent after backout'
complete -c hg -n 'contains \'backout\' (commandline -poc)' -s m -l message --description 'Use <text> as commit message'
complete -c hg -n 'contains \'backout\' (commandline -poc)' -s l -l logfile --description 'Read commit message from <file>'
complete -c hg -n 'contains \'backout\' (commandline -poc)' -s d -l date --description 'Record datecode as commit date'
complete -c hg -n 'contains \'backout\' (commandline -poc)' -l parent --description 'Parent to choose when backing out merge'
complete -c hg -n 'contains \'backout\' (commandline -poc)' -s u -l user --description 'Record user as committer'
complete -c hg -n 'contains \'backout\' (commandline -poc)' -s I -l include --description 'Include names matching the given patterns'
complete -c hg -n 'contains \'backout\' (commandline -poc)' -s X -l exclude --description 'Exclude names matching the given patterns'


#
# Completions for the 'bundle' subcommand
#

complete -c hg -n 'contains \'bundle\' (commandline -poc)' -s f -l force --description 'Run even when remote repository is unrelated'


#
# Completions for the 'cat' subcommand
#

complete -c hg -n 'contains \'cat\' (commandline -poc)' -s o -l output --description 'Print output to file with formatted name'
complete -c hg -n 'contains \'cat\' (commandline -poc)' -s r -l rev --description 'Print the given revision'
complete -c hg -n 'contains \'cat\' (commandline -poc)' -s I -l include --description 'Include names matching the given patterns'
complete -c hg -n 'contains \'cat\' (commandline -poc)' -s X -l exclude --description 'Exclude names matching the given patterns'


#
# Completions for the 'clone' subcommand
#

complete -c hg -n 'contains \'clone\' (commandline -poc)' -s U -l noupdate --description 'Do not update the new working directory'
complete -c hg -n 'contains \'clone\' (commandline -poc)' -s r -l rev --description 'A changeset you would like to have after cloning'
complete -c hg -n 'contains \'clone\' (commandline -poc)' -l pull --description 'Use pull protocol to copy metadata'
complete -c hg -n 'contains \'clone\' (commandline -poc)' -l uncompressed --description 'Use uncompressed transfer (fast over LAN)'
complete -c hg -n 'contains \'clone\' (commandline -poc)' -s e -l ssh --description 'Specify ssh command to use'
complete -c hg -n 'contains \'clone\' (commandline -poc)' -l remotecmd --description 'Specify hg command to run on the remote side'


#
# Completions for the 'commit' subcommand
#

complete -c hg -n 'contains \'commit\' (commandline -poc)' -s A -l addremove --description 'Mark new/missing files as added/removed before committing'
complete -c hg -n 'contains \'commit\' (commandline -poc)' -s m -l message --description 'Use <text> as commit message'
complete -c hg -n 'contains \'commit\' (commandline -poc)' -s l -l logfile --description 'Read the commit message from <file>'
complete -c hg -n 'contains \'commit\' (commandline -poc)' -s d -l date --description 'Record datecode as commit date'
complete -c hg -n 'contains \'commit\' (commandline -poc)' -s u -l user --description 'Record user as commiter'
complete -c hg -n 'contains \'commit\' (commandline -poc)' -s I -l include --description 'Include names matching the given patterns'
complete -c hg -n 'contains \'commit\' (commandline -poc)' -s X -l exclude --description 'Exclude names matching the given patterns'


#
# Completions for the 'copy' subcommand
#

complete -c hg -n 'contains \'copy\' (commandline -poc)' -s A -l after --description 'Record a copy that has already occurred'
complete -c hg -n 'contains \'copy\' (commandline -poc)' -s f -l force --description 'Forcibly copy over an existing managed file'
complete -c hg -n 'contains \'copy\' (commandline -poc)' -s I -l include --description 'Include names matching the given patterns'
complete -c hg -n 'contains \'copy\' (commandline -poc)' -s X -l exclude --description 'Exclude names matching the given patterns'
complete -c hg -n 'contains \'copy\' (commandline -poc)' -s n -l dry-run --description 'Do not perform actions, just print output'


#
# Completions for the 'diff' subcommand
#

complete -c hg -n 'contains \'diff\' (commandline -poc)' -s r -l rev --description 'Revision'
complete -c hg -n 'contains \'diff\' (commandline -poc)' -s a -l text --description 'Treat all files as text'
complete -c hg -n 'contains \'diff\' (commandline -poc)' -s p -l show-function --description 'Show which function each change is in'
complete -c hg -n 'contains \'diff\' (commandline -poc)' -s w -l ignore-all-space --description 'Ignore white space when comparing lines'
complete -c hg -n 'contains \'diff\' (commandline -poc)' -s b -l ignore-space-change --description 'Ignore changes in the amount of white space'
complete -c hg -n 'contains \'diff\' (commandline -poc)' -s B -l ignore-blank-lines --description 'Ignore changes whose lines are all blank'
complete -c hg -n 'contains \'diff\' (commandline -poc)' -s I -l include --description 'Include names matching the given patterns'
complete -c hg -n 'contains \'diff\' (commandline -poc)' -s X -l exclude --description 'Exclude names matching the given patterns'


#
# Completions for the 'export' subcommand
#

complete -c hg -n 'contains \'export\' (commandline -poc)' -s o -l output --description 'Print output to file with formatted name'
complete -c hg -n 'contains \'export\' (commandline -poc)' -s a -l text --description 'Treat all files as text'
complete -c hg -n 'contains \'export\' (commandline -poc)' -l switch-parent --description 'Diff against the second parent'


#
# Completions for the 'grep' subcommand
#

complete -c hg -n 'contains \'grep\' (commandline -poc)' -s 0 -l print0 --description 'End fields with NUL'
complete -c hg -n 'contains \'grep\' (commandline -poc)' -l all --description 'Print all revisions that match'
complete -c hg -n 'contains \'grep\' (commandline -poc)' -s i -l ignore-case --description 'Ignore case when matching'
complete -c hg -n 'contains \'grep\' (commandline -poc)' -s l -l files-with-matches --description 'Print only filenames and revs that match'
complete -c hg -n 'contains \'grep\' (commandline -poc)' -s n -l line-number --description 'Print matching line numbers'
complete -c hg -n 'contains \'grep\' (commandline -poc)' -s r -l rev --description 'Search in given revision range'
complete -c hg -n 'contains \'grep\' (commandline -poc)' -s u -l user --description 'Print user who committed change'
complete -c hg -n 'contains \'grep\' (commandline -poc)' -s I -l include --description 'Include names matching the given patterns'
complete -c hg -n 'contains \'grep\' (commandline -poc)' -s X -l exclude --description 'Exclude names matching the given patterns'


#
# Completions for the 'heads' subcommand
#

complete -c hg -n 'contains \'heads\' (commandline -poc)' -s b -l branches --description 'Show branches'
complete -c hg -n 'contains \'heads\' (commandline -poc)' -l style --description 'Display using template map file'
complete -c hg -n 'contains \'heads\' (commandline -poc)' -s r -l rev --description 'Show only heads which are descendants of rev'
complete -c hg -n 'contains \'heads\' (commandline -poc)' -l template --description 'Display with template'


#
# Completions for the 'help' subcommand
#



#
# Completions for the 'identify' subcommand
#



#
# Completions for the 'import' subcommand
#

complete -c hg -n 'contains \'import\' (commandline -poc)' -s p -l strip --description 'Directory strip option for patch. This has the same'
complete -c hg -n 'contains \'import\' (commandline -poc)' -s m -l message --description 'Use <text> as commit message'
complete -c hg -n 'contains \'import\' (commandline -poc)' -s b -l base --description 'Base path'
complete -c hg -n 'contains \'import\' (commandline -poc)' -s f -l force --description 'Skip check for outstanding uncommitted changes'


#
# Completions for the 'incoming' subcommand
#

complete -c hg -n 'contains \'incoming\' (commandline -poc)' -s M -l no-merges --description 'Do not show merges'
complete -c hg -n 'contains \'incoming\' (commandline -poc)' -s f -l force --description 'Run even when remote repository is unrelated'
complete -c hg -n 'contains \'incoming\' (commandline -poc)' -l style --description 'Display using template map file'
complete -c hg -n 'contains \'incoming\' (commandline -poc)' -s n -l newest-first --description 'Show newest record first'
complete -c hg -n 'contains \'incoming\' (commandline -poc)' -l bundle --description 'File to store the bundles into'
complete -c hg -n 'contains \'incoming\' (commandline -poc)' -s p -l patch --description 'Show patch'
complete -c hg -n 'contains \'incoming\' (commandline -poc)' -s r -l rev --description 'A specific revision you would like to pull'
complete -c hg -n 'contains \'incoming\' (commandline -poc)' -l template --description 'Display with template'
complete -c hg -n 'contains \'incoming\' (commandline -poc)' -s e -l ssh --description 'Specify ssh command to use'
complete -c hg -n 'contains \'incoming\' (commandline -poc)' -l remotecmd --description 'Specify hg command to run on the remote side'


#
# Completions for the 'init' subcommand
#

complete -c hg -n 'contains \'init\' (commandline -poc)' -s e -l ssh --description 'Specify ssh command to use'
complete -c hg -n 'contains \'init\' (commandline -poc)' -l remotecmd --description 'Specify hg command to run on the remote side'


#
# Completions for the 'locate' subcommand
#

complete -c hg -n 'contains \'locate\' (commandline -poc)' -s r -l rev --description 'Search the repository as it stood at rev'
complete -c hg -n 'contains \'locate\' (commandline -poc)' -s 0 -l print0 --description 'End filenames with NUL, for use with xargs'
complete -c hg -n 'contains \'locate\' (commandline -poc)' -s f -l fullpath --description 'Print complete paths from the filesystem root'
complete -c hg -n 'contains \'locate\' (commandline -poc)' -s I -l include --description 'Include names matching the given patterns'
complete -c hg -n 'contains \'locate\' (commandline -poc)' -s X -l exclude --description 'Exclude names matching the given patterns'


#
# Completions for the 'log' subcommand
#

complete -c hg -n 'contains \'log\' (commandline -poc)' -s b -l branches --description 'Show branches'
complete -c hg -n 'contains \'log\' (commandline -poc)' -s k -l keyword --description 'Search for a keyword'
complete -c hg -n 'contains \'log\' (commandline -poc)' -s l -l limit --description 'Limit number of changes displayed'
complete -c hg -n 'contains \'log\' (commandline -poc)' -s r -l rev --description 'Show the specified revision or range'
complete -c hg -n 'contains \'log\' (commandline -poc)' -s M -l no-merges --description 'Do not show merges'
complete -c hg -n 'contains \'log\' (commandline -poc)' -l style --description 'Display using template map file'
complete -c hg -n 'contains \'log\' (commandline -poc)' -s m -l only-merges --description 'Show only merges'
complete -c hg -n 'contains \'log\' (commandline -poc)' -s p -l patch --description 'Show patch'
complete -c hg -n 'contains \'log\' (commandline -poc)' -l template --description 'Display with template'
complete -c hg -n 'contains \'log\' (commandline -poc)' -s I -l include --description 'Include names matching the given patterns'
complete -c hg -n 'contains \'log\' (commandline -poc)' -s X -l exclude --description 'Exclude names matching the given patterns'


#
# Completions for the 'manifest' subcommand
#



#
# Completions for the 'merge' subcommand
#

complete -c hg -n 'contains \'merge\' (commandline -poc)' -s b -l branch --description 'Merge with head of a specific branch'
complete -c hg -n 'contains \'merge\' (commandline -poc)' -s f -l force --description 'Force a merge with outstanding changes'


#
# Completions for the 'outgoing' subcommand
#

complete -c hg -n 'contains \'outgoing\' (commandline -poc)' -s M -l no-merges --description 'Do not show merges'
complete -c hg -n 'contains \'outgoing\' (commandline -poc)' -s f -l force --description 'Run even when remote repository is unrelated'
complete -c hg -n 'contains \'outgoing\' (commandline -poc)' -s p -l patch --description 'Show patch'
complete -c hg -n 'contains \'outgoing\' (commandline -poc)' -l style --description 'Display using template map file'
complete -c hg -n 'contains \'outgoing\' (commandline -poc)' -s r -l rev --description 'A specific revision you would like to push'
complete -c hg -n 'contains \'outgoing\' (commandline -poc)' -s n -l newest-first --description 'Show newest record first'
complete -c hg -n 'contains \'outgoing\' (commandline -poc)' -l template --description 'Display with template'
complete -c hg -n 'contains \'outgoing\' (commandline -poc)' -s e -l ssh --description 'Specify ssh command to use'
complete -c hg -n 'contains \'outgoing\' (commandline -poc)' -l remotecmd --description 'Specify hg command to run on the remote side'


#
# Completions for the 'parents' subcommand
#

complete -c hg -n 'contains \'parents\' (commandline -poc)' -s b -l branches --description 'Show branches'
complete -c hg -n 'contains \'parents\' (commandline -poc)' -s r -l rev --description 'Show parents from the specified rev'
complete -c hg -n 'contains \'parents\' (commandline -poc)' -l style --description 'Display using template map file'
complete -c hg -n 'contains \'parents\' (commandline -poc)' -l template --description 'Display with template'


#
# Completions for the 'paths' subcommand
#



#
# Completions for the 'pull' subcommand
#

complete -c hg -n 'contains \'pull\' (commandline -poc)' -s u -l update --description 'Update the working directory to tip after pull'
complete -c hg -n 'contains \'pull\' (commandline -poc)' -s e -l ssh --description 'Specify ssh command to use'
complete -c hg -n 'contains \'pull\' (commandline -poc)' -s f -l force --description 'Run even when remote repository is unrelated'
complete -c hg -n 'contains \'pull\' (commandline -poc)' -s r -l rev --description 'A specific revision you would like to pull'
complete -c hg -n 'contains \'pull\' (commandline -poc)' -l remotecmd --description 'Specify hg command to run on the remote side'


#
# Completions for the 'push' subcommand
#

complete -c hg -n 'contains \'push\' (commandline -poc)' -s f -l force --description 'Force push'
complete -c hg -n 'contains \'push\' (commandline -poc)' -s e -l ssh --description 'Specify ssh command to use'
complete -c hg -n 'contains \'push\' (commandline -poc)' -s r -l rev --description 'A specific revision you would like to push'
complete -c hg -n 'contains \'push\' (commandline -poc)' -l remotecmd --description 'Specify hg command to run on the remote side'


#
# Completions for the 'recover' subcommand
#



#
# Completions for the 'remove' subcommand
#

complete -c hg -n 'contains \'remove\' (commandline -poc)' -s A -l after --description 'Record remove that has already occurred'
complete -c hg -n 'contains \'remove\' (commandline -poc)' -s f -l force --description 'Remove file even if modified'
complete -c hg -n 'contains \'remove\' (commandline -poc)' -s I -l include --description 'Include names matching the given patterns'
complete -c hg -n 'contains \'remove\' (commandline -poc)' -s X -l exclude --description 'Exclude names matching the given patterns'


#
# Completions for the 'rename' subcommand
#

complete -c hg -n 'contains \'rename\' (commandline -poc)' -s A -l after --description 'Record a rename that has already occurred'
complete -c hg -n 'contains \'rename\' (commandline -poc)' -s f -l force --description 'Forcibly copy over an existing managed file'
complete -c hg -n 'contains \'rename\' (commandline -poc)' -s I -l include --description 'Include names matching the given patterns'
complete -c hg -n 'contains \'rename\' (commandline -poc)' -s X -l exclude --description 'Exclude names matching the given patterns'
complete -c hg -n 'contains \'rename\' (commandline -poc)' -s n -l dry-run --description 'Do not perform actions, just print output'


#
# Completions for the 'revert' subcommand
#

complete -c hg -n 'contains \'revert\' (commandline -poc)' -s r -l rev --description 'Revision to revert to'
complete -c hg -n 'contains \'revert\' (commandline -poc)' -l no-backup --description 'Do not save backup copies of files'
complete -c hg -n 'contains \'revert\' (commandline -poc)' -s I -l include --description 'Include names matching given patterns'
complete -c hg -n 'contains \'revert\' (commandline -poc)' -s X -l exclude --description 'Exclude names matching given patterns'
complete -c hg -n 'contains \'revert\' (commandline -poc)' -s n -l dry-run --description 'Do not perform actions, just print output'


#
# Completions for the 'rollback' subcommand
#



#
# Completions for the 'root' subcommand
#



#
# Completions for the 'serve' subcommand
#

complete -c hg -n 'contains \'serve\' (commandline -poc)' -s A -l accesslog --description 'Name of access log file to write to'
complete -c hg -n 'contains \'serve\' (commandline -poc)' -s d -l daemon --description 'Run server in background'
complete -c hg -n 'contains \'serve\' (commandline -poc)' -l daemon-pipefds --description 'Used internally by daemon mode'
complete -c hg -n 'contains \'serve\' (commandline -poc)' -s E -l errorlog --description 'Name of error log file to write to'
complete -c hg -n 'contains \'serve\' (commandline -poc)' -s p -l port --description 'Port to use (default: 8000)'
complete -c hg -n 'contains \'serve\' (commandline -poc)' -s a -l address --description 'Address to use'
complete -c hg -n 'contains \'serve\' (commandline -poc)' -s n -l name --description 'Name to show in web pages (default: working dir)'
complete -c hg -n 'contains \'serve\' (commandline -poc)' -l webdir-conf --description 'Name of the webdir config file (serve more than one repo)'
complete -c hg -n 'contains \'serve\' (commandline -poc)' -l pid-file --description 'Name of file to write process ID to'
complete -c hg -n 'contains \'serve\' (commandline -poc)' -l stdio --description 'For remote clients'
complete -c hg -n 'contains \'serve\' (commandline -poc)' -s t -l templates --description 'Web templates to use'
complete -c hg -n 'contains \'serve\' (commandline -poc)' -l style --description 'Template style to use'
complete -c hg -n 'contains \'serve\' (commandline -poc)' -s 6 -l ipv6 --description 'Use IPv6 in addition to IPv4'


#
# Completions for the 'status' subcommand
#

complete -c hg -n 'contains \'status\' (commandline -poc)' -s m -l modified --description 'Show only modified files'
complete -c hg -n 'contains \'status\' (commandline -poc)' -s a -l added --description 'Show only added files'
complete -c hg -n 'contains \'status\' (commandline -poc)' -s r -l removed --description 'Show only removed files'
complete -c hg -n 'contains \'status\' (commandline -poc)' -s d -l deleted --description 'Show only deleted (but tracked) files'
complete -c hg -n 'contains \'status\' (commandline -poc)' -s u -l unknown --description 'Show only unknown (not tracked) files'
complete -c hg -n 'contains \'status\' (commandline -poc)' -s i -l ignored --description 'Show ignored files'
complete -c hg -n 'contains \'status\' (commandline -poc)' -s n -l no-status --description 'Hide status prefix'
complete -c hg -n 'contains \'status\' (commandline -poc)' -s C -l copies --description 'Show source of copied files'
complete -c hg -n 'contains \'status\' (commandline -poc)' -s 0 -l print0 --description 'End filenames with NUL, for use with xargs'
complete -c hg -n 'contains \'status\' (commandline -poc)' -s I -l include --description 'Include names matching the given patterns'
complete -c hg -n 'contains \'status\' (commandline -poc)' -s X -l exclude --description 'Exclude names matching the given patterns'


#
# Completions for the 'tag' subcommand
#

complete -c hg -n 'contains \'tag\' (commandline -poc)' -s l -l local --description 'Make the tag local'
complete -c hg -n 'contains \'tag\' (commandline -poc)' -s m -l message --description 'Message for tag commit log entry'
complete -c hg -n 'contains \'tag\' (commandline -poc)' -s d -l date --description 'Record datecode as commit date'
complete -c hg -n 'contains \'tag\' (commandline -poc)' -s u -l user --description 'Record user as commiter'
complete -c hg -n 'contains \'tag\' (commandline -poc)' -s r -l rev --description 'Revision to tag'


#
# Completions for the 'tags' subcommand
#



#
# Completions for the 'tip' subcommand
#

complete -c hg -n 'contains \'tip\' (commandline -poc)' -s b -l branches --description 'Show branches'
complete -c hg -n 'contains \'tip\' (commandline -poc)' -l style --description 'Display using template map file'
complete -c hg -n 'contains \'tip\' (commandline -poc)' -s p -l patch --description 'Show patch'
complete -c hg -n 'contains \'tip\' (commandline -poc)' -l template --description 'Display with template'


#
# Completions for the 'unbundle' subcommand
#

complete -c hg -n 'contains \'unbundle\' (commandline -poc)' -s u -l update --description 'Update the working directory to tip after unbundle'


#
# Completions for the 'update' subcommand
#

complete -c hg -n 'contains \'update\' (commandline -poc)' -s b -l branch --description 'Checkout the head of a specific branch'
complete -c hg -n 'contains \'update\' (commandline -poc)' -s m -l merge --description 'Allow merging of branches (DEPRECATED)'
complete -c hg -n 'contains \'update\' (commandline -poc)' -s C -l clean --description 'Overwrite locally modified files'
complete -c hg -n 'contains \'update\' (commandline -poc)' -s f -l force --description 'Force a merge with outstanding changes'


#
# Completions for the 'verify' subcommand
#



#
# Completions for the 'version' subcommand
#




