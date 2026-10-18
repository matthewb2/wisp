# Flex/Bison 설정 (필수)
find_package(FLEX REQUIRED)
find_package(BISON REQUIRED)

# Lexer/Parser 생성
flex_target(nslog_lexer src/filter-lexer.l ${CMAKE_CURRENT_BINARY_DIR}/filter-lexer.c)
bison_target(nslog_parser src/filter-parser.y ${CMAKE_CURRENT_BINARY_DIR}/filter-parser.c)
add_flex_bison_dependency(nslog_lexer nslog_parser)

# 라이브러리 소스 정의
set(NSLOG_SOURCES
    src/core.c
    src/filter.c
    ${FLEX_nslog_lexer_OUTPUTS}
    ${BISON_nslog_parser_OUTPUTS}
)

# 라이브러리 생성
add_library(nslog STATIC ${NSLOG_SOURCES})

# 헤더 경로 및 정의
target_include_directories(nslog PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# 컴파일 정의 (필요시)
target_compile_definitions(nslog PRIVATE _DEFAULT_SOURCE)