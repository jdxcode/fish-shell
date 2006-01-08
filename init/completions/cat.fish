complete -c cat -s A -l show-all -d (_ "Escape all non-printing characters")
complete -c cat -s b -l number-nonblank -d (_ "Number nonblank lines")
complete -c cat -s e -d (_ "Escape non-printing characters except tab")
complete -c cat -s E -l show-ends -d (_ "Display $ at end of line")
complete -c cat -s n -l number -d (_ "Number all lines")
complete -c cat -s s -l squeeze-blank -d (_ "Never more than single blank line")
complete -c cat -s t -d (_ "Escape non-printing characters except newline")
complete -c cat -s T -l show-tabs -d (_ "Escape tab")
complete -c cat -s v -d (_ "Escape non-printing except newline and tab")
complete -c cat -l help -d (_ "Display help and exit")
complete -c cat -l version -d (_ "Display version and exit")

