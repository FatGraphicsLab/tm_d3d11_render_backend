# Development Notes


## 2021-07-17

DXGI

* https://docs.microsoft.com/en-us/windows/win32/direct3ddxgi/dx-graphics-dxgi
* IDXGIFactory1::EnumAdapters()，同一张 Intel 集成显卡，会返回多个的 IDXGIAdapter，原因不明
* NVIDIA控制面板设置默认显卡，会影响 EnumAdapters() 显卡 index 的值，从而影响创建所选取的默认显卡

DXGI Architecture

![](images/developing_notes/dxgi-dll.png)

DXGI Terminology

![](images/developing_notes/dxgi-terms.png)



## 2021-07-16

* tm_d3d11_api 基本框框搭建



## 2021-07-12

* window system runs~
