# syntax errors — shell must continue after each
< <
> >
| echo bad
echo trailing |
# command not found
notacommand
# redirection to unwritable path (continue after error)
echo test > /root/no_permission_here.txt
echo recovered after bad redirect
# cd errors
cd /nonexistent
cd too many arguments here
echo done
