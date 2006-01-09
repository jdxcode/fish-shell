#apt-src
complete -c apt-src -s h -l help -d (_ "Display help and exit")
complete -f -c apt-src -a "update" -d (_ "Update list of src pkgs")
complete -f -c apt-src -a "install" -d (_ "Install src pkgs")
complete -f -c apt-src -a "upgrade" -d (_ "Upgrade src pkgs")
complete -f -c apt-src -a "remove" -d (_ "Remove src pkgs")
complete -f -c apt-src -a "build" -d (_ "Build src pkgs")
complete -f -c apt-src -a "clean" -d (_ "Clean src pkgs")
complete -f -c apt-src -a "import" -d (_ "Detect known src tree")
complete -f -c apt-src -a "list" -d (_ "List installed src pkg\(s\)")
complete -f -c apt-src -a "location" -d (_ "Root src tree")
complete -f -c apt-src -a "version" -d (_ "Version of src pkg")
complete -f -c apt-src -a "name" -d (_ "Name of the src pkg")
complete -f -c apt-src -s b -l build -d (_ "Build src pkgs")
complete -f -c apt-src -s i -l installdebs -d (_ "Install after build")
complete -f -c apt-src -s p -l patch -d (_ "Patch local changes")
complete -r -c apt-src -s l -l location -d (_ "Specify a dir")
complete -c apt-src -s c -l here -d (_ "Run on current dir")
complete -f -c apt-src -l upstream-version -d (_ "Omit debian version")
complete -f -c apt-src -s k -l keep-built -d (_ "Do not del built files")
complete -f -c apt-src -s n -l no-delete-source -d (_ "Do not del source files")
complete -f -c apt-src -l version -d (_ "Source tree version")
complete -f -c apt-src -s q -l quiet -d (_ "Output to /dev/null")
complete -f -c apt-src -s t -l trace -d (_ "Output trace")
