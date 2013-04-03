function __fish_complete_ftp -d 'Complete ftp, pftp' --argument-names ftp
        complete -c $ftp -xa "(__fish_print_hostnames)" -d 'Hostname'
tname'
 complete -c $ftp -s 4 -d 'Use only IPv4 to contact any host'
 host'
 complete -c $ftp -s 6 -d 'Use IPv6 only'
 only'
 complete -c $ftp -s p -d 'Use passive mode for data transfers'
sfers'
 complete -c $ftp -s A -d 'Use active mode for data transfers'
sfers'
 complete -c $ftp -s i -d 'Turn off interactive prompting during multiple file transfers'
        complete -c $ftp -s n -d 'Restrain ftp from attempting "auto-login" upon initial connection'
        complete -c $ftp -s e -d 'Disable command editing and history support'
        complete -c $ftp -s g -d 'Disable file name globbing'
Disable file name globbing'
m -d 'Do not explicitly bind data and control channels to same interface'
channels to same interface'
v -d 'Verbose. Show all server responses and data transfer stats'
es and data transfer stats'
d -d 'Enable debugging'
end
