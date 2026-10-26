# Windows release acceptance

A green CI build is not interactive desktop certification. Complete this checklist on Windows before publishing a stable release.

v0.2.2 adds actual HWND interaction tests on the hosted Windows desktop, including screenshot selection, exported annotation thickness, and the recording dock's visibility, topmost state, capture-excluded pixels and pause/resume/stop bindings. This is stronger than the earlier headless rendering/startup checks, but does not replace physical audio devices or mixed-DPI hardware acceptance.

- [ ] Clean Windows 11 x64: unzip, launch without admin, tray visible, no missing runtime.
- [ ] 100%, 125%, 150%, 200% scaling; two monitors with mixed DPI and negative coordinates.
- [ ] Region, screen and window-region snips; escape, reselect, every annotation, text/Chinese IME, undo/redo.
- [ ] Export PNG dimensions, colors and orientation; paste into Paint; verify redaction in exported pixels.
- [ ] Busy clipboard: PNG still saved, clipboard failure reported accurately.
- [ ] Silent, speaker loopback, microphone and mixed recordings; verify actual audible tracks.
- [ ] RGB frame orientation, cursor hotspot, control-window capture exclusion.
- [ ] 15/30/60 FPS and all bitrates; odd-sized region rounded down to even dimensions.
- [ ] Pause for 30 seconds and resume; no stale paused audio or gap in playback.
- [ ] One-hour 1080p30 recording: measure A/V drift, process-tree memory and CPU, no duration-proportional memory growth.
- [ ] Hotkey conflict, invalid shortcut, changed shortcut and tray actions while main window hidden.
- [ ] Stop while recording; close control window; attempt quit during setup/capture/encoding.
- [ ] No default audio device, denied microphone, unplugged headset, locked desktop, display disconnected.
- [ ] Read-only destination, nonexistent destination, full disk, Unicode path, long path.
- [ ] Verify partial-file and finalization errors are understandable; never claim crash recovery.
- [ ] Measure combined binary/zip size and idle resource use; record hardware and OS build.
- [ ] Check unsigned binary warnings without recommending disabling Windows security.

## v0.2.2 回归验收

- 屏幕截图点击一次进入标注，Enter 保存；Esc 取消后可立即重新截图。
- 区域截图拖动后绘制矩形，点击 9px，确认已有矩形立即变粗；V 选择另一标注可改色与粗细，Ctrl+Z/Y 恢复样式。
- 右键重选后 Ctrl+Z 不崩溃，不恢复上一个选区的标注。
- 在每个显示器上分别开始屏幕录制，控制台应出现在对应显示器工作区内，暂停/继续/停止均可操作。
- 录制桌面，移动控制台并切换声音；回放 MP4 确认没有控制台或黑色遮挡块。
- 无法设置捕获排除时，应明确取消录制，不能偷偷隐藏控制台继续录制。

## v0.2.0 新增验收

- 拖动悬浮控制台，不遮挡其他窗口操作；Windows 10/11 分别确认控制台不会出现在录制画面内。
- 以无声音开始，依次开启电脑声音、麦克风、两者、全部关闭；逐段回放确认真正有声/静音。
- 暂停时开关声音，再继续，确认暂停段未写入文件、没有旧音频回放。
- 分别在录制中和暂停中停止，确认 MP4 可播放；无麦克风权限或无默认设备时仍能保存画面。
- 拔出音频设备，确认只关闭该声音来源；连接后重试开启。
- 新增椭圆、自动序号、颜色/粗细选择、Shift 约束与 Ctrl+Z/Y 导出像素正确。
- 在 640 DIP 窗口宽度及 125%/150% 显示缩放下检查主界面和标注工具栏。

## v0.3.0 回归验收

- 启动直接显示悬浮栏；截图取消/完成、录屏结束后仍是同一个窗口，不再弹出主界面。
- 左右贴边、悬停展开、点击固定、手动收纳；录制侧签有状态，展开后停止可用。
- 关闭主窗口但保留悬浮栏；保存后重启位置正确；断开显示器回到工作区。
- 在 100/125/150/200% 缩放下检查工具栏、颜色面板、拾色与命中位置；跨屏选择后跟随选区所在显示器缩放。
- 试验过小选区、重选、屏幕截图点击与取消后再次截图，不残留选区状态。
- 滑块连续调整、松手一次撤销；颜色弹层预设/RGB/HEX；拾色来自原图而非覆盖标注。
- 已有对象移动/缩放、箭头两端点、Delete、Ctrl+D、方向键微调、双击中英文文字再编辑；撤销/重做导出像素正确。
- 调整选区角点后撤销，选区与标注都正确恢复。
- Ctrl+C 仅复制且不创建文件；Ctrl+S 保存并复制；繁忙剪贴板提示准确、重试成功。
- 连续录制、暂停、收纳、展开、声音切换与停止后解码回放；工具栏和侧签都没有入镜或留下黑块。

自动化覆盖项以当次 CI 日志为准；以上清单不因构建成功而自动视为实机通过。
