#Completions for ruby
complete -c ruby -s 0 -d (_ 'Specify record separator')
complete -c ruby -s a -d (_ 'Feed input to split')
complete -c ruby -s c -d (_ 'Check syntax')
complete -c ruby -s K -d (_ 'Kanji code-set')
complete -c ruby -s d -l debug -d (_ 'Debugger')
complete -c ruby -s e -x -d (_ 'Execute command')
complete -c ruby -s h -l help -d (_ 'Display help')
complete -c ruby -s F -d (_ 'Set regexp used to split input')
complete -c ruby -s i -d (_ 'Edit files in-place')
complete -c ruby -s I -d (_ 'Include path')
complete -c ruby -s l -d (_ 'Line ending processing')
complete -c ruby -s n -d (_ 'Loop script')
complete -c ruby -s p -d (_ 'Loop script, print $_')
complete -c ruby -s r -r -d (_ 'Require file')
complete -c ruby -s s -d (_ 'Define custom switches')
complete -c ruby -s S -d (_ 'Search $PATH for script')
complete -c ruby -s T -d (_ 'Taint checking')
complete -c ruby -s v -l verbose -d (_ 'Verbose mode')
complete -c ruby -s w -d (_ 'Verbose mode without message')
complete -c ruby -l version -d (_ 'Display version and exit')
complete -c ruby -s x -d (_ 'Extract script')
complete -c ruby -s X -x -a '(__fish_complete_directory (commandline -ct))' -d (_ 'Directory')
complete -c ruby -s y -l yydebug -d (_ 'Compiler debug mode')

