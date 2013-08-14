if test (uname) = Darwin
	function prompt_pwd --description "Print the current working directory, shortend to fit the prompt"
		echo $PWD | sed -e "s|^$HOME|~|" -e 's|^/private||' -e 's-\([^/.]\)[^/]*/-\1/-g'
	end
else if test (uname -o) = Cygwin
	function prompt_pwd --description "Print the current working directory, shortend to fit the prompt"
		echo $PWD | sed -e "s|^$HOME|~|" -e 's|^/cygdrive/\(.\)|\1/:|' -e 's-\([^/.]\)[^/]*/-\1/-g' -e 's-^\([^/]\)/:/\?-\u\1:/-'
	end
else
	function prompt_pwd --description "Print the current working directory, shortend to fit the prompt"
		echo $PWD | sed -e "s|^$HOME|~|" -e 's-\([^/.]\)[^/]*/-\1/-g'
	end
end
