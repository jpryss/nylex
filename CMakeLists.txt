# nylex with CMake (the Makefile needs nothing but make; this is for
# those who prefer CMake):
#   cmake -B build -DCMAKE_INSTALL_PREFIX=/usr/local && cmake --build build
#   ctest --test-dir build;  cmake --install build
cmake_minimum_required(VERSION 3.5)
project(nylex C)

include(GNUInstallDirs)
set(LEXLIB "${CMAKE_INSTALL_FULL_DATADIR}/nylex" CACHE PATH "where the system files and dictionaries go")
option(USE_TERMIO "System V termio instead of termios (old systems)" OFF)

add_executable(nylex src/main.c src/menu.c src/edit.c src/esc.c src/esc2.c src/cut.c src/calc.c src/rec.c
	src/input.c src/vdu.c src/ui.c src/sys.c src/listp.c src/print.c src/pdf.c src/work.c src/doc.c src/reg.c src/term.c src/vmf.c)
target_compile_definitions(nylex PRIVATE LEXLIB="${LEXLIB}")
add_executable(lexvmf src/lexvmf.c src/vmf.c)
add_executable(lexspell src/lexspell.c)
add_executable(lexconv src/lexconv.c)
target_compile_definitions(lexspell PRIVATE LEXLIB="${LEXLIB}")
if(USE_TERMIO)
	target_compile_definitions(nylex PRIVATE USE_TERMIO)
endif()

install(TARGETS nylex lexvmf lexspell lexconv DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES nylex.vmf nylex-en.vmf DESTINATION ${LEXLIB})
install(FILES doc/nylex.1 doc/lexvmf.1 doc/lexspell.1 doc/lexconv.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
install(FILES doc/TUTORIAL.md DESTINATION ${CMAKE_INSTALL_DOCDIR})

# the dictionaries, made of dict/ by tools/mkdict.py (python3)
find_program(PYTHON3 python3)
if(PYTHON3)
	set(dicts)
	foreach(d sv:sv_SE:sv en-GB:en_GB:en en-US:en_US:en)
		string(REPLACE ":" ";" d ${d})
		list(GET d 0 name)
		list(GET d 1 src)
		list(GET d 2 code)
		add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/${name}.dict
			COMMAND ${PYTHON3} ${CMAKE_SOURCE_DIR}/tools/mkdict.py ${CMAKE_SOURCE_DIR}/dict/${src}.aff
				${CMAKE_SOURCE_DIR}/dict/${src}.dic ${CMAKE_BINARY_DIR}/${name}.dict ${code}
			DEPENDS ${CMAKE_SOURCE_DIR}/dict/${src}.aff ${CMAKE_SOURCE_DIR}/dict/${src}.dic)
		list(APPEND dicts ${CMAKE_BINARY_DIR}/${name}.dict)
	endforeach()
	add_custom_target(dicts ALL DEPENDS ${dicts})
	install(FILES ${dicts} DESTINATION ${LEXLIB})

	enable_testing()
	foreach(cs latin1 utf8 swe7)
		add_test(NAME smoke-${cs} COMMAND ${PYTHON3} ${CMAKE_SOURCE_DIR}/tests/smoke.py
			$<TARGET_FILE:nylex> ${CMAKE_SOURCE_DIR}/nylex.vmf ${cs})
	endforeach()
	add_test(NAME editing COMMAND ${PYTHON3} ${CMAKE_SOURCE_DIR}/tests/edtest.py
		$<TARGET_FILE:nylex> ${CMAKE_SOURCE_DIR}/nylex.vmf)
	add_test(NAME editing-en COMMAND ${PYTHON3} ${CMAKE_SOURCE_DIR}/tests/edtest.py
		$<TARGET_FILE:nylex> ${CMAKE_SOURCE_DIR}/nylex-en.vmf)
	add_test(NAME pdf COMMAND ${PYTHON3} ${CMAKE_SOURCE_DIR}/tests/pdftest.py
		$<TARGET_FILE:nylex> ${CMAKE_SOURCE_DIR}/nylex.vmf)
	add_test(NAME conversion COMMAND ${PYTHON3} ${CMAKE_SOURCE_DIR}/tests/convtest.py $<TARGET_FILE:lexconv>)
	add_test(NAME spelling COMMAND ${PYTHON3} ${CMAKE_SOURCE_DIR}/tests/spelltest.py
		$<TARGET_FILE:lexspell> ${CMAKE_BINARY_DIR})
endif()
