; Prosty MBR: laduje sektor startowy aktywnej partycji (LBA z fallbackiem CHS)
; Copyright (c) 2026 Lukasz Bolesta, with the help of Claude (Anthropic). MIT.
        bits 16
        org 0x0600
start:
        cli
        xor  ax, ax
        mov  ss, ax
        mov  sp, 0x7C00
        mov  ds, ax
        mov  es, ax
        sti
        cld
        mov  si, 0x7C00
        mov  di, 0x0600
        mov  cx, 256
        rep  movsw
        jmp  0:reloc
reloc:
        mov  [drive], dl
        mov  si, 0x07BE
        mov  cx, 4
.find:  cmp  byte [si], 0x80
        je   .found
        add  si, 16
        loop .find
        mov  si, msg_nopart
        jmp  error
.found:
        mov  [part], si
        mov  di, 3                    ; proby
.retry:
        ; LBA (INT 13h rozszerzenia)
        mov  ah, 0x41
        mov  bx, 0x55AA
        mov  dl, [drive]
        int  0x13
        jc   .chs
        cmp  bx, 0xAA55
        jne  .chs
        test cl, 1
        jz   .chs
        mov  si, [part]
        mov  eax, [si+8]
        mov  [dap_lba], eax
        mov  si, dap
        mov  ah, 0x42
        mov  dl, [drive]
        int  0x13
        jnc  .loaded
.chs:
        mov  si, [part]
        mov  dh, [si+1]
        mov  cx, [si+2]
        mov  dl, [drive]
        mov  bx, 0x7C00
        mov  ax, 0x0201
        int  0x13
        jnc  .loaded
        xor  ax, ax
        mov  dl, [drive]
        int  0x13
        dec  di
        jnz  .retry
        mov  si, msg_read
        jmp  error
.loaded:
        cmp  word [0x7DFE], 0xAA55
        mov  si, msg_nosig
        jne  error
        mov  dl, [drive]
        mov  si, [part]
        jmp  0:0x7C00

error:
        lodsb
        test al, al
        jz   .halt
        mov  ah, 0x0E
        mov  bx, 7
        int  0x10
        jmp  error
.halt:  hlt
        jmp  .halt

drive:  db 0
part:   dw 0
dap:    db 0x10, 0
        dw 1            ; sektorow
        dw 0x7C00, 0    ; bufor
dap_lba: dd 0, 0
msg_nopart: db "MBR: brak aktywnej partycji", 0
msg_read:   db "MBR: blad odczytu", 0
msg_nosig:  db "MBR: brak sygnatury", 0

        times 0x1B8-($-$$) db 0
        dd 0x45424F58           ; identyfikator dysku
        dw 0
        times 64 db 0           ; tablica partycji (wypelniana przez skrypt)
        dw 0xAA55
