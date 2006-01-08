complete -c complete -s c -l command -d (_ "Command to add completion to") -r
complete -c complete -s p -l path -d (_ "Path to add completion to")
complete -c complete -s s -l short-option -d (_ "Posix-style option to complete")
complete -c complete -s l -l long-option -d (_ "GNU-style option to complete")
complete -c complete -s o -l old-option -d (_ "Old style long option to complete")
complete -c complete -s f -l no-files -d (_ "Do not use file completion")
complete -c complete -s r -l require-parameter -d (_ "Require parameter")
complete -c complete -s x -l exclusive -d (_ "Require parameter and do not use file completion")
complete -c complete -s a -l arguments -d (_ "A list of possible arguments")
complete -c complete -s d -l description -d (_ "Description of this completions")
complete -c complete -s u -l unauthorative -d (_ "Option list is not complete")
complete -c complete -s e -l erase -d (_ "Remove completion")
complete -c complete -s h -l help -d (_ "Display help and exit")
