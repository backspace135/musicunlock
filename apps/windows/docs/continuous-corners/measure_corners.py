from pathlib import Path
import os
import subprocess
import shutil
import sys

root = Path.cwd()
sys.path.insert(0, str(root / 'scripts'))
from build import msvc_environment

source = (root / 'app/Corners.cpp').read_text(encoding='utf-8')
native = source[source.index('winrt::com_ptr<ID2D1Factory> factory()'):source.index('composition::CompositionPathGeometry path_geometry')]
cpp = r'''
#include <windows.h>
#include <d2d1.h>
#include <winrt/base.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cassert>
#include <vector>
#include "ContinuousPath.h"
'''+ native + r'''
int main() {
    // 直接调用从生产文件提取的构建函数，检查直线连接、四角和内缩路径。
    for (const float radius : {0.f, 2.f, 20.f, 1000.f}) {
        auto shape = build(200, 120, radius, 0);
        D2D1_RECT_F bounds{};
        winrt::check_hresult(shape->GetBounds(nullptr, &bounds));
        assert(std::abs(bounds.left) < 0.001f && std::abs(bounds.top) < 0.001f);
        assert(std::abs(bounds.right - 200) < 0.001f && std::abs(bounds.bottom - 120) < 0.001f);
        for (const auto point : {D2D1::Point2F(100, 1), D2D1::Point2F(199, 60),
                                 D2D1::Point2F(100, 119), D2D1::Point2F(1, 60), D2D1::Point2F(100, 60)}) {
            BOOL inside = FALSE;
            winrt::check_hresult(shape->FillContainsPoint(point, nullptr, &inside));
            assert(inside);
        }
    }
    auto inner = build(200, 120, 2, 4);
    D2D1_RECT_F bounds{};
    winrt::check_hresult(inner->GetBounds(nullptr, &bounds));
    assert(bounds.left == 4 && bounds.top == 4 && bounds.right == 196 && bounds.bottom == 116);
    std::puts("D2D integration: bounds, edge connections, center fill, small-radius inset passed");
    using clock = std::chrono::steady_clock;
    std::vector<double> samples;
    for (int i = -1000; i < 10000; ++i) {
        const auto start = clock::now();
        auto geometry = build(200.f + (i & 511), 120.f + (i & 127), 20.f, 0.f);
        const double us = std::chrono::duration<double, std::micro>(clock::now() - start).count();
        if (i >= 0) samples.push_back(us);
    }
    std::sort(samples.begin(), samples.end());
    double total = 0;
    for (const double sample : samples) total += sample;
    std::printf("D2D path rebuild: n=%zu mean=%.3f us p50=%.3f us p95=%.3f us p99=%.3f us\n",
                samples.size(), total / samples.size(), samples[5000], samples[9500], samples[9900]);
}
'''
out = root / 'build/tmp'
(out / 'corners_perf.cpp').write_text(cpp, encoding='utf-8')
env = msvc_environment(os.environ.copy())
subprocess.run([shutil.which('cl', path=env['PATH']), '/nologo', '/std:c++20', '/EHsc', '/O2', '/utf-8', '/I'+str(root/'app'),
                str(out/'corners_perf.cpp'), str(root/'app/ContinuousPath.cpp'),
                '/Fe:'+str(out/'corners_perf.exe'), '/Fo:'+str(out)+'\\',
                '/link', 'd2d1.lib', 'ole32.lib', 'runtimeobject.lib', 'oleaut32.lib'], env=env, cwd=out, check=True)
subprocess.run([str(out/'corners_perf.exe')], env=env, check=True)
