
complete -c type -s h -l help -d (_ "Print this message")
complete -c type -s a -l all -d (_ "Print all possible definitions of the specified name")
complete -c type -s f -l no-functions -d (_ "Supress function and builtin lookup")
complete -c type -s t -l type -d (_ "Print command type")
complete -c type -s p -l path -d (_ "Print path to command, or nothing if name is not a command")
complete -c type -s P -l force-path -d (_ "Print path to command")
