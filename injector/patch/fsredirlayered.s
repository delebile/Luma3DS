.nds
.create "fsredirlayered.bin", 0

.macro addr, reg, func
    add reg, pc, #func-.-8
.endmacro
.macro load, reg, func
    ldr reg, [pc, #func-.-8]
.endmacro
.macro svc, svcnum
    .word 0xef000000 + svcnum
.endmacro

.arm
_start:

    ; Jumps here before returning from fsMountRomFs
    hookMountSdmc:
        b       mountSdmc
        nop     ; Substituted opcode
        nop     ; Branch to hooked function

    ; Jumps here before every iFileOpen call
    hookRedirectFile:
        b       redirectFIle
        nop     ; Substituted opcode
        nop     ; Branch to hooked function

    ; Mounts SDMC and registers the archive as 'YS:'
    mountSdmc:
        mov     r1, #9
        mov     r0, sp 
        load    r4, fsMountArchive
        blx     r4
        mov     r2, #0
        ldr     r1, [sp]
        addr    r0, sdmcArchiveName
        load    r4, fsRegisterArchive
        blx     r4
        mov     r0, #0
        b       hookMountSdmc+4

    ; Check filepath passed to iFileOpen.
    ; If it's trying to access a RomFS or SaveData file, we try to
    ; open it from the title folder on the sdcard.
    ; If the file cannot be opened from the sdcard, we just open
    ; it from its original archive like nothing happened
    redirectFile:
        ldrb    r12, [r1]
        cmp     r12, #0x72   ; 'r', initial of 'rom:'
        beq     handleRomFsFile
        cmp     r12, #0x64   ; 'd', initial of 'data:'
        beq     handleSaveDataFile
        b       hookRedirectFile+4
    backToFileOpen:
        cmp     r0, #0
        add     sp, sp, #0x400
        ldmfd   sp!, {r0-r12, lr}
        bxeq    lr
        b       hookRedirectFile+4

    ; Code is trying to access RomFs file, we try to open it from sdcard
    handleRomFsFile:
        stmfd   sp!, {r0-r12, lr}
        sub     sp, sp, #0x400
        mov     r12, sp
        bl      editFilePath
        bl      hookRedirectFile+4 ; call iFileOpen
        b       backToFileOpen

    ; Code is trying to access SaveData file, we try to open it from sdcard
    handleSaveDataFile:
        stmfd   sp!, {r0-r12, lr}
        sub     sp, sp, #0x400
        mov     r12, sp
        bl      editFilePath
        load    r12, saveFolderName
        str     r12, [r1, #0x42]
        load    r12, saveFolderName+4
        str     r12, [r1, #0x46]
        bl      hookRedirectFile+4 ; call iFileOpen
        b       backToFileOpen

    ; Crafts the redirected filepath and puts it in r1
    editFilePath:
        stmfd   sp!, {r0, r2, r4}
        mov     r0, r12
        load    r4, sdmcCustomPath
        editFilePathLoop1:
            ldrb    r2, [r4], #1
            strh    r2, [r0], #2
            cmp     r2, #0
            bne     editFilePathLoop1
        sub     r0, r0, #2
        editFilePathLoop2:
            ldrh    r2, [r1], #2
            cmp     r2, #0x3A  ; ':'
            bne     editFilePathLoop2
        editFilePathLoop3:
            ldrh    r2, [r1], #2
            strh    r2, [r0], #2
            cmp     r2, #0
            bne     editFilePathLoop3
        ldmfd   sp!, {r0, r2, r4}
        mov     r1, r12
        bx      lr

.pool
.align 4
    sdmcArchiveName :       .dcb "YS:", 0
    saveFolderName  :       .dcw "save" ; overwriting this on 'romfs' will give 'saves'
    dummyWord :             .word 0xdeadbeef
    sdmcCustomPath :        .word 0x00000000
    fsMountArchive :        .word 0x00000000
    fsRegisterArchive :     .word 0x00000000
    iFileOpen :             .word 0x00000000

.close
