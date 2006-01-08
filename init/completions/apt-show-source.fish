#apt-show-source
complete -c apt-show-source -s h -l help -d (_ 'Apt-show-source command help')
complete -r -c apt-show-source -l status-file -d (_ 'Read pkg from FILE') -f
complete -r -c apt-show-source -o stf -d (_ 'Read pkg from FILE') -f
complete -r -c apt-show-source -l list-dir -a '(ls -Fp .|grep /$) /var/lib/apt/lists' -d (_ 'Specify APT list dir')
complete -r -c apt-show-source -o ld -a '(ls -Fp .|grep /$) /var/lib/apt/lists' -d (_ 'Specify APT list dir')
complete -r -c apt-show-source -s p -l package -a '(apt-cache pkgnames)' -d (_ 'List PKG info')
complete -f -c apt-show-source -l version-only -d (_ 'Print version only')
complete -f -c apt-show-source -s a -l all -d (_ 'Print all src pkgs with version')
complete -f -c apt-show-source -s v -l verbose -d (_ 'Verbose message')
