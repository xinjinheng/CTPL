@echo off

rem 设置编译器路径
gcc -v >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo 找不到GCC编译器
    echo 请确保已安装MinGW-w64并将其添加到PATH环境变量
    pause
    exit /b 1
)

echo 使用GCC编译CTPL分布式示例...

rem 编译基础示例（不需要Boost）
gcc -std=c++17 -o example.exe example.cpp
if %ERRORLEVEL% NEQ 0 (
    echo 基础示例编译失败
    pause
    exit /b 1
)
echo 基础示例编译成功: example.exe

rem 编译分布式示例（需要Boost）
set BOOST_INCLUDE_DIR=C:\boost_1_78_0
echo 正在寻找Boost库...
if not exist "%BOOST_INCLUDE_DIR%\boost\lockfree\queue.hpp" (
    echo 未找到Boost库，请修改BOOST_INCLUDE_DIR变量指向正确的Boost安装目录
    echo 当前BOOST_INCLUDE_DIR: %BOOST_INCLUDE_DIR%
    pause
    exit /b 1
)

gcc -std=c++17 -I%BOOST_INCLUDE_DIR% -o example_distributed.exe example_distributed.cpp -lstdc++fs -lpthread
if %ERRORLEVEL% NEQ 0 (
    echo 分布式示例编译失败
    pause
    exit /b 1
)
echo 分布式示例编译成功: example_distributed.exe

echo 编译完成！
pause
