bits 32
section .text
    global start
    extern kernel_main

start:
    call kernel_main
    hlt
