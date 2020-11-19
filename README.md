# Description

This program helps you run a lot of time consuming shell scripts in
parallel. You select how many threads you want to spare and then
produce a list of tasks to execute. The program will go through the
list at the fastest pace possible with the number of selected threads.
On error or abortion, the failed/unprocessed tasks will be stored so
you can pick up the work.

```
 # Produce a large list of tasks to execute
 # Maybe you have a large database of users
 # SELECT id FROM user WHERE someCriteria=true
 # Export as CSV

 echo "" > commands.list # clear the list
 USERS=$(cat users.csv | cut -d, -f1 | tail -n +1)
 for user in $USERS; do
    echo 'curl --header "Authorization: Bearer ......" --fail https://external-api.com/'"$user >  $user.json" >> commands.list
 done

 # Execute all of the commands
 ./tparallel --thread 10 --commands ./commands.list --failures failures.list --unprocessed unprocessed.list
 # or if you don't care about failures
 # ./tparallel --thread 10 --commands ./commands.list
```

# Compile using
gcc -Werror -Wall tparallel.c -lpthread -o tparallel

If you get a lot of warnings, either fix them or run without -Werror -Wall

# Run using

```
./tparallel --thread 10 --commands commands-to-execute.list --failures failures.list --unprocessed unprocessed.list
```

You may skip --failures and --unprocessed if you don't really care about that

The commands-to-execute.list should contain all the commands to execute, one per line.

Example:
```
  curl --fail http://external-api.com/user1 > user1.json
  curl --fail http://external-api.com/user1/image.png > image.png
  ...
  curl --fail http://external-api.com/user10000 > user1.json
  curl --fail http://external-api.com/user10000/image.png > image.png
```

Or if you want to have complex logic for each task, it's much easier to create a separate shell:

get-user.sh:
```
    curl --fail http://external-api.com/$1 > $1.json
    IMG=$(cat $1.json | jq .profilePicture)
    NAME=$(cat $1.json | jq .username)
    wget $IMG -O "${NAME}.png"
```

commands-to-execute.list:
```
    ./get-user.sh 1
    ./get-user.sh 2
    ....
    ./get-user.sh 10000
```