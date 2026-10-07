# Chocolatine

Chocolatine 🍫 is a simple French programming language, fully open-source and made in C, currently available on Windows and Linux. (I haven't compiled it for macOS, but it should be possible—it's C, after all...)

I created this language to make programming easily understandable in French. It's a personal project to develop my own skills.

I'm open to any contributions—just open a pull request. If you need any help, I'm available on Discord. To start learning Chocolatine, please read the documentation. I made it VERY short (trust me).


# HOW TO USE

⚠️ Documentation: https://louloubiwan.github.io/Chocolatine-documentation/   ⚠️

Discord: https://discord.com/invite/4xXHPc8JZY


## Building

**Windows building** : 


`gcc -std=c99 -Wall chocolatine.c mpc.c -o chocolatine`

**Windows building from Linux (cross-compile)** : 

`x86_64-w64-mingw32-gcc -std=c99 -Wall -Wextra -pedantic chocolatine.c mpc.c -o chocolatine.exe \
    -lm \
    -I.
`

**Linux building** : 

- build-essential : `sudo apt install build-essential`


`gcc -std=c99 -Wall -Wextra -pedantic chocolatine.c mpc.c -o chocolatine \
    -lm -ledit \
    -D_GNU_SOURCE \
    -D_DEFAULT_SOURCE \
    -I.`
