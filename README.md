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

Just use tail as follows

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
