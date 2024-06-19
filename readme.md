# FileBay2 🥳

Update file in a quick way

+ Super Light🍃: zero-dependency on databases.
+ Super Fast⚡: backend in pure C, no external stylesheet or script for frontend.

![demo](/doc/demo.png)


## Run 🦉

**(Optional)** change the `CONFIG` to meet your demand:

```bash
# DO NOT INCLUDE COMMENT!

file_max_byte:10485760  # Maximum file size in bytes
file_max_count:10       # Maximum number of files
file_expire:60          # File expiration period in minutes
worker_period:30        # Cleanser worker check interval in minutes
storage_dir:./files     # Directory for storing files
dump_dist:./dump.bin    # Location of the dump file
```

Then, compile and run:

```bash
gcc *.c -Iinclude -o Filebay
./Filebay <PORT>
```

## Debug 🐞
To access comprehensive runtime information, compile the executable in debug mode:

```bash
gcc *.c -Iinclude -o Filebay -g -DDEBUG
./server_debug
```