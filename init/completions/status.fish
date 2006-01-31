
complete -c status -l is-command-substitution -d (_ "Test if a command substitution is currently evaluated")
complete -c status -l is-block -d (_ "Test if a code block is currently evaluated")
complete -c status -l is-interactive -d (_ "Test if this is an interactive shell")
complete -c status -l is-login -d (_ "Test if this is a login shell")
complete -c status -l is-full-job-control -d (_ "Test if all new jobs are put under job control")
complete -c status -l is-interactive-job-control -d (_ "Test if only interactive new jobs are put under job control")
complete -c status -l is-no-job-control -d (_ "Test if new jobs are never put under job control")
complete -c status -s j -l job-control -a "full interactive none" -d (_ "Set which jobs are out under job control") 
complete -c status -s t -l print-stack-trace -d (_ "Print a list of all function calls leading up to running the current command")
