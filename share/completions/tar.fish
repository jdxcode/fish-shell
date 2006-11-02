
# Peek inside of archives and list all files
complete -c tar -a "(__fish_complete_tar)"

complete -c tar -s A -l catenate -d (N_ "Append archive to archive")
complete -c tar -l concatenate -d (N_ "Append archive to archive")
complete -c tar -s c -l create -d (N_ "Create archive")
complete -c tar -s d -l diff -d (N_ "Compare archive and filesystem")
complete -c tar -l compare -d (N_ "Compare archive and filesystem")
complete -c tar -l delete -d (N_ "Delete from archive")
complete -c tar -s r -l append -d (N_ "Append files to archive")
complete -c tar -s t -l list -d (N_ "List archive")
complete -c tar -s u -l update -d (N_ "Append new files")
complete -c tar -s x -l extract -d (N_ "Extract from archive")
complete -c tar -l get -d (N_ "Extract from archive")
complete -c tar -l atime-preserve -d (N_ "Keep access time")
complete -c tar -s b -l block-size -d (N_ "Block size")
complete -c tar -s B -l read-full-blocks -d (N_ "Reblock while reading")
complete -c tar -s C -l directory -r -d (N_ "Change directory")
complete -c tar -l checkpoint -d (N_ "Print directory names")
complete -c tar -s f -l file -r -d (N_ "Archive file")
complete -c tar -l force-local -d (N_ "Archive is local")
complete -c tar -s F -l info-script -d (N_ "Run script at end of tape")
complete -c tar -s G -l incremental -d (N_ "Use old incremental GNU format")
complete -c tar -s g -l listed-incremental -d (N_ "Use new incremental GNU format")
complete -c tar -s h -l dereference -d (N_ "Dereference symlinks")
complete -c tar -s i -l ignore-zeros -d (N_ "Ignore zero block in archive")
complete -c tar -s j -l bzip -d (N_ "Filter through bzip2")
complete -c tar -l ignore-failed-read -d (N_ "Don't exit on unreadable files")
complete -c tar -s k -l keep-old-files -d (N_ "Don't overwrite")
complete -c tar -s K -l starting-file -r -d (N_ "Starting file in archive")
complete -c tar -s l -l one-file-system -d (N_ "Stay in local filesystem")
complete -c tar -s L -l tape-length -r -d (N_ "Tape length")
complete -c tar -s m -l modification-time -d (N_ "Don't extract modification time")
complete -c tar -s M -l multi-volume -d (N_ "Multi volume archive")
complete -c tar -s N -l after-date -r -d (N_ "Only store newer files")
complete -c tar -s o -l old-archive -d (N_ "Use V7 format")
complete -c tar -l portability -d (N_ "Use V7 format")
complete -c tar -s O -l to-stdout -d (N_ "Extract to stdout")
complete -c tar -s p -l same-permissions -d (N_ "Extract all permissions")
complete -c tar -l preserve-permissions -d (N_ "Extract all permissions")
complete -c tar -s P -l absolute-paths -d (N_ "Don't strip leading /")
complete -c tar -l preserve -d (N_ "Preserve all permissions and do not sort file arguments")
complete -c tar -s R -l record-number -d (N_ "Show record number")
complete -c tar -l remove-files -d (N_ "Remove files after adding to archive")
complete -c tar -s s -l same-order -d (N_ "Do not sort file arguments")
complete -c tar -l preserve-order -d (N_ "Do not sort file arguments")
complete -c tar -l same-owner -d (N_ "Preserve file ownership")
complete -c tar -s S -l sparse -d (N_ "Handle sparse files")
complete -c tar -s T -l files-from -r -d (N_ "Extract file from file")
complete -c tar -l null -d (N_ "-T has null-terminated names")
complete -c tar -l totals -d (N_ "Print total bytes written")
complete -c tar -s v -l verbose -d (N_ "Verbose mode")
complete -c tar -s V -l label -r -d (N_ "Set volume name")
complete -c tar -l version -d (N_ "Display version and exit")
complete -c tar -s w -l interactive -d (N_ "Ask for confirmation")
complete -c tar -l confirmation -d (N_ "Ask for confirmation")
complete -c tar -s W -l vevrify -d (N_ "Verify archive")
complete -c tar -l exclude -r -d (N_ "Exclude file")
complete -c tar -s X -l exclude-from -r -d (N_ "Exclude files listed in specified file")
complete -c tar -s Z -l compress -d (N_ "Filter through compress")
complete -c tar -l uncompress -d (N_ "Filter through compress")
complete -c tar -s z -l gzip -d (N_ "Filter through gzip")
complete -c tar -l gunzip -d (N_ "Filter through gzip")
complete -c tar -l use-compress-program -r -d (N_ "Filter through specified program")

