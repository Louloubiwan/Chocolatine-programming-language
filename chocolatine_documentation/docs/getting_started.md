# Installation

First you need to download Chocolatine from github

[Download Chocolatine](https://github.com/Louloubiwan/Chocolatine){ .md-button .md-button--primary }



## Windows

execute `chocolatine.exe`

##  Linux

execute `execute chocolatine.sh`


To learn how to code in Chocolatine, make sure to read the documentation : 

[Documentation](/documentation/){ .md-button .md-button--primary }




------------------------------------------------------

# Compile Chocolatine (For developers only)

## Install GCC on Linux

GCC compiler is already installed on Linux, but if it doesn't, use this commande to install GCC.

  *  `sudo apt install build-essential`

## Install GCC on Windows

[Windows download link](https://sourceforge.net/projects/mingw-w64/)



## Compile Chocolatine

To make sure that GCC is installed on your desktop, execute this commande in the terminal : 

  *  `gcc --version`



Compile commande : 

 * `gcc -std=c99 -Wall chocolatine.c mpc.c -o chocolatine`


 after that, the compiled `chocolatine` should appear in the folder

