# Copy the desktop's runtime data into an app bundle or asset folder, keeping
# the relative paths the desktop already looks for (fonts/, assets/, build/wallpapers).
#   cmake -DROOT=<repo> -DDEST=<dir> [-DLIST=<file>] -P copy-resources.cmake
# LIST, when given, receives one relative path per line (Android unpacks with it).
set(FILES
    fonts/ChicagoFLF.ttf
    fonts/LICENSE.txt
    assets/icons/ospi-icons.icl8
    assets/icons/cursors/arrow.curs
    assets/sounds/classic9/01-startup_chord.wav
    assets/sounds/classic9/03-plain_beep.wav
    assets/sounds/classic9/16-menu_open.wav
    assets/sounds/classic9/17-menu_select.wav)
file(GLOB WALLPAPERS RELATIVE ${ROOT} ${ROOT}/build/wallpapers/*.bmp)
list(APPEND FILES ${WALLPAPERS})
set(listing "")
foreach(f ${FILES})
    if(EXISTS ${ROOT}/${f})
        get_filename_component(dir ${DEST}/${f} DIRECTORY)
        file(MAKE_DIRECTORY ${dir})
        file(COPY_FILE ${ROOT}/${f} ${DEST}/${f} ONLY_IF_DIFFERENT)
        string(APPEND listing "${f}\n")
    else()
        message(WARNING "OS999: ${f} not found (run make wallpapers on a Mac for build/wallpapers)")
    endif()
endforeach()
if(LIST)
    file(WRITE ${LIST} "${listing}")
endif()
