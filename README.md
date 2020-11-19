# Compile using
gcc -Werror -Wall tparallel.c -lpthread -o tparallel

If you get a lot of warnings, either fix them or run without -Werror -Wall

# Run using

./tparallel --thread 10 --commands commands-to-execute.list --failures failures.list --unprocessed unprocessed.list

You may skip --failures and --unprocessed if you don't really care about that

The commands-to-execute.list should contain all the commands to execute, one per line.

Example:

  curl --fail http://external-api.com/user1 > user1.json
  curl --fail http://external-api.com/user1/image.png > image.png
  ...
  curl --fail http://external-api.com/user10000 > user1.json
  curl --fail http://external-api.com/user10000/image.png > image.png


Or if you want to have complex logic for each task, it's much easier to create a separate shell:

get-user.sh:
    curl --fail http://external-api.com/$1 > $1.json
    IMG=$(cat $1.json | jq .profilePicture)
    NAME=$(cat $1.json | jq .username)
    wget $IMG -O "${NAME}.png"

commands-to-execute.list:
    ./get-user.sh 1
    ./get-user.sh 2
    ....
    ./get-user.sh 10000
