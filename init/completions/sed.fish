complete -c sed -s n -l quiet -d (_ "Silent mode")
complete -c sed -l silent -d (_ "Silent mode")
complete -c sed -s e -l expression -x -d (_ "Evaluate expression")
complete -c sed -s f -l file -r -d (_ "Evalute file")
complete -c sed -s i -l in-place -d (_ "Edit files in place")
complete -c sed -s l -l line-length -x -d (_ "Specify line-length")
complete -c sed -l posix -d (_ "Disable all GNU extensions")
complete -c sed -s r -l regexp-extended -d (_ "Use extended regexp")
complete -c sed -s s -l separate -d (_ "Consider files as separate")
complete -c sed -s u -l unbuffered -d (_ "Use minimal IO buffers")
complete -c sed -l help -d (_ "Display help and exit")
complete -c sed -s V -l version -d (_ "Display version and exit")
