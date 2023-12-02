> under developing...
> 
# FileBay 🥳
Update file in a quick way

+ Super Light🍃: executable size <30kb, no other stuff other than file sharing.
+ Super Fast⚡: backend in pure C, no external stylesheet or script for fontend.

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
```bash
# make the executable
make

# start the server
./server PORT
```