## mysy windows 编译 asteria 教程

1. 用mingw64的msys2打开，cd到asteria下

2. 设置一下镜像
sed -i "s#https\?://mirror.msys2.org/#https://mirrors.tuna.tsinghua.edu.cn/msys2/#g" /etc/pacman.d/mirrorlist*

3. 装一下这些包
pacman -S meson gcc pkgconf pcre2-devel openssl-devel zlib-devel  \
        libiconv-devel libedit-devel

4. 执行meson setup build_debug
报错1：
ERROR: This python3 seems to be msys/python on MSYS2 Windows, but you are in a MinGW environment
ERROR: Please install and use mingw-w64-x86_64-python3 and/or mingw-w64-x86_64-meson with Pacman

解决方案：安装一下 pacman -S mingw-w64-x86_64-python3 mingw-w64-x86_64-meson
然后关闭msys mingw，重新打开，注意还是mingw版本的msys

报错2：
Run-time dependency libpcre2-8 found: NO (tried pkgconfig)
则执行：export PKG_CONFIG_PATH=/usr/lib/pkgconfig:$PKG_CONFIG_PATH

5. 编译asteria：meson compile -Cbuild_debug

6. 启动asteria REPL： ./build_debug/asteria

7. REPL交互，需要以 :开头  如查看帮助 :help

7. 输入一块代码 

:heredoc #end    // 表示代码文件开头, 然后才可以像idle那样
var test = 1;
std.io.putfln("$1", test); // 比较弱鸡的是 fmt 需要用$来占位才能输出
#end // 表示一段代码文件结束

8. 读取文件  :source build_debug/code/switch.txt // 需要注意可执行文件重定向路径到asteria下了
