# tailx
A utility to tail multiple files

## Decription

tailx only purpose is to tail multiple files and append the filename at the beginning of each line.
I created this utility to be used in kubernetes where I want to tail multiple files to the stdout.

## Usage

```
tailx file1.txt file2.txt
```

### Sample output

```
file1.txt: content in file1.txt
file2.txt: content in file2.txt
```

## Alternative

1. Just use tail as follows

   ```bash
   tail -F file1.txt file2.txt
   ```

   This will output as follows:
   ```
   ==> file1.txt <==
   content in file1.txt

   ==> file2.txt <==
   content in file2.txt
   ```

2. Use tail with awk (see [this post](https://unix.stackexchange.com/a/195930))

   ```bash
   tail -F file1.txt file2.txt | awk '/^==> / {a=substr($0, 5, length-8); next} {print a": "$0}'
   ```

   This will output as follows:
   ```
   file1.txt: content in file1.txt
   file1.txt:
   file2.txt: content in file2.txt
   file2.txt:
   ```
   NOTE: The empty lines are part of the section separator, not the actual content of the files.
