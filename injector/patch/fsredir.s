.nds
.create "fsredir.bin", 0

.macro addr, reg, func
    add reg, pc, #func-.-8
.endmacro
.macro load, reg, func
    ldr reg, [pc, #func-.-8]
.endmacro
.macro svc, svcnum
    .word 0xef000000 + svcnum
.endmacro

; Patch by delebile

.arm
_start:

    ; Jumps here before the fsMountRomFs call
    _mountSd:
        b       mountSd
        .word   0xdead0000  ; Substituted opcode
        .word   0xdead0001  ; Branch to hooked function

    ; Jumps here before every iFileOpen call
    _fsRedir:
        b       fsRedir
        .word   0xdead0002  ; Substituted opcode
        .word   0xdead0003  ; Branch to hooked function

    ; Mounts SDMC and registers the archive as 'YS:'
    mountSd:
        stmfd   sp!, {r0-r4, lr}
        sub     sp, sp, #4
        mov     r1, #9
        mov     r0, sp
        load    r4, fsMountArchive
        blx     r4
        mov     r3, #0
        mov     r2, #0
        ldr     r1, [sp]
        addr    r0, sdmcArchiveName
        load    r4, fsRegisterArchive
        blx     r4
        add     sp, sp, #4
        ldmfd   sp!, {r0-r4, lr}
        b       _mountSd+4

    ; Check the path passed to iFileOpen.
    ; If it's trying to access a RomFS or SaveData file, we try to
    ; open it from the title folder on the sdcard.
    ; If the file cannot be opened from the sdcard, we just open
    ; it from its original archive like nothing happened
    fsRedir:
        stmfd   sp!, {r0-r12, lr}
        ldrb    r12, [r1]
        cmp     r12, #0x72  ; 'r', will include "rom:" and "rom2:"
		bne 	endRedir
        sub     sp, sp, #0x400
        pathRedir:
            stmfd   sp!, {r0-r3}
            add     r0, sp, #0x10
            load    r3, sdmcCustomPath
            pathRedir_1:
                ldrb    r2, [r3], #1
                strh    r2, [r0], #2
                cmp     r2, #0
                bne     pathRedir_1
            sub     r0, r0, #2
            pathRedir_2:
                ldrh    r2, [r1], #2
                cmp     r2, #0x3A  ; ':'
                bne     pathRedir_2
            pathRedir_3:
                ldrh    r2, [r1], #2
                strh    r2, [r0], #2
                cmp     r2, #0
                bne     pathRedir_3
            ldmfd   sp!, {r0-r3}
        mov     r1, sp
        bl      _fsRedir+4
        add     sp, sp, #0x400
        cmp     r0, #0

    endRedir:
        ldmfd   sp!, {r0-r12, lr}
        moveq   r0, #0
        bxeq    lr
        b       _fsRedir+4

.pool
.align 4
    sdmcArchiveName :       .dcb "sdmc:", 0 .align 4
    sdmcCustomPath :        .word 0xdead0004
    fsMountArchive :        .word 0xdead0005
    fsRegisterArchive :     .word 0xdead0006

.close
