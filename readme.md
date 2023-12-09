> under developing...

# FileBay 🥳

Update file in a quick way

+ Super Light🍃: executable size < 30kb, zero-dependency on databases.
+ Super Fast⚡: backend in pure C, no external stylesheet or script for frontend.

![demo](/doc/demo.png)

## Dependencies 🏗️

- **libmicrohttpd**: Used for HTTP server functionality. [GNU LGPL v2.1](https://www.gnu.org/software/libmicrohttpd/).
- **zlib**: For compression request. [zlib License](https://zlib.net/zlib_license.html).

Please ensure these are installed on your system for the software to function correctly. you may also install via the code below.

```bash
# install the dependency
chmod +x INSTALL
./INSTALL
```

## Run 🐎

**(Optional)** change the `CONFIG` to meet your demand:

```bash
file_max_byte:10485760   # Maximum file size, in byte
file_expire:1440         # Worker will delete the file after this time, in minute
worker_period:720        # Worker running period, in minute
storage_dir:./files      # Where should uploaded files be stored
dump_dist:./dump.bin	 # Where should the serialized binary be stored
max_file_count:100       # Max file number serve
```

Then, compile and run:

```bash
# make the executable
make

# start the server
./server PORT
```

If `Make` is not installed on your system, consider using the [nobuild toolkit](https://github.com/tsoding/nobuild). Essentially, it functions as a C-driven alternative to Make, offering support for both POSIX-compliant systems and Windows environments using MSVC.

+ `$ cc nobuild.c -o nobuild` on POSIX systems
+ `$ cl.exe nobuild.c` on Windows with MSVC

Then, Run the build: `$ ./nobuild`
