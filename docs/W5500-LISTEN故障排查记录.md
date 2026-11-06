# W5500 TCP 服务器无法进入 LISTEN 状态 —— 故障分析与修复报告

- 项目：FreeRTOS_F407VET6 + W5500 网络模块（SPI2，Mode0，约 2.6 MHz，CS = PE13）
- 工程：C:\Users\86185\Desktop\第二个项目\task
- 涉及文件：Core\Src\W5500.c、Core\Inc\W5500.H
- 故障首次观测：2026-11-06 22:36；修复验证：2026-11-06 22:51 / 22:58
- 一句话结论：W5500.H 中宏 `SOCK_LISTEN` 被重复定义，符号最终取值为 0x14（状态值），导致向 Sn_CR 写入非法命令 0x14，LISTEN 命令从未真正执行。

## 1. 故障现象

上电后串口输出：

```
[W5500] VERSIONR=0x04 (期望 0x04)
[W5500] IP = 192.168.1.200
[W5500] PHYCFGR=0xBF  LINK=UP
[W5500] PHYCFGR=0xBF LINK 100M Full

[NET] sn0 LISTEN fail: MR=0x01 CR=0x00 SR=0x13 PORT=5000 IR=0x00 RXB=2 TXB=2
[NET] sn1 cmd 0x10 超时 (CR=0x10)
[NET] sn1 cmd 0x01 超时 (CR=0x01)
[NET] OPEN fail: SR=0x81
[NET] sn2 cmd 0x10 超时 (CR=0x10)
[NET] sn2 cmd 0x01 超时 (CR=0x01)
[NET] OPEN fail: SR=0x04
[NET] OPEN fail: SR=0x00
[NET] sn4 LISTEN fail: MR=0x01 CR=0x00 SR=0x13 PORT=5000 IR=0x00 RXB=2 TXB=2
```

核心特征：socket 0 的 MR/端口/缓冲配置全部正确（MR=0x01 是 TCP，PORT=5000），
OPEN 已经成功（SR 停在 0x13 = SOCK_INIT），但 LISTEN 永远不生效，
SR 一直停在 0x13，无法进入 0x14，PC 端自然连不上。

## 2. 现象分析：三条关键线索

1. **VERSIONR / PHYCFGR 都正确** → SPI 读寄存器通路、硬件连接、PHY 链路均正常，
   可以从"信号完整性 / 时序 / 网线"这类怀疑里排除了。
2. **SR 不变，但 CR 读到 0x00** → 写完命令后 CR 又被硬件清零了，说明芯片
   "收到了写操作并把 CR 清掉"，只是没有产生任何动作。这指向"命令码本身非法"，
   而不是"命令没写进去"。
3. **打印行里没有 MSS 字段** → 说明板子里跑的不是最新源码编译的固件
   （加 MSSR 之后调试打印本应包含 `MSS=%u`）。这是"改了代码没效果"的一半原因：
   前半程一直在调试旧固件。教训见第 6 节第 6 条。

## 3. 根因

### 3.1 现场代码

```c
/* W5500.H 第 39 行附近：Sn_CR 命令码 */
#define SOCK_LISTEN        0x02        /* LISTEN 命令 */

/* W5500.H 第 49 行附近：Sn_SR 状态值 */
#define SOCK_LISTEN        0x14        /* LISTEN 状态 */
```

同一个宏名 `SOCK_LISTEN` 在同一个头文件里定义了两次。

### 3.2 预处理器怎么处理它

C 语言允许宏重定义，只有当两次定义的 token 序列完全相同时才视为"相同宏"；
两次取值不同（0x02 vs 0x14）时，编译器给出重定义诊断，但仍然以**最后一次定义**为准继续编译。

Keil MDK 的表现是：

```
warning:  #47-D: incompatible redefinition of macro "SOCK_LISTEN"
```

——一条 warning，淹没在成百上千条编译输出里，没有人注意到。

最终生效：`SOCK_LISTEN == 0x14`

### 3.3 于是实际下发的命令是错的

```c
static uint8_t w5500_cmd(uint8_t sn, uint8_t cmd);   /* W5500.c */

w5500_cmd(sn, SOCK_LISTEN);      /* W5500.c:245 */
```

展开后：`Sn_CR = 0x14`

### 3.4 0x14 为什么无效

W5500 数据手册 Sn_CR 合法命令码只有：

| 命令 | 值 | 说明 |
|---|---|---|
| OPEN | 0x01 | 按 Sn_MR 打开 socket |
| LISTEN | 0x02 | TCP 服务器监听 |
| CONNECT | 0x04 | TCP 客户端连接 |
| DISCON | 0x08 | 断开 |
| CLOSE | 0x10 | 关闭 socket |
| SEND | 0x20 | 发送 |
| SEND_MAC | 0x21 | UDP 带 MAC 发送 |
| SEND_KEEP | 0x22 | 发送保活包 |
| RECV | 0x40 | 接收完成确认 |

0x14 不在表中 → 芯片判定为非法命令，**丢弃该命令并把 Sn_CR 自动清零**，
socket 状态机保持原状：SR 恒为 0x13 (SOCK_INIT)。

### 3.5 为什么这个 bug "不报错"（本次最迷惑人的地方）

`w5500_cmd()` 的成功判据是"写 CR → 等待 CR 被硬件清零"：

```c
W5500_WriteReg8(sn, Sn_CR, cmd);
while (W5500_ReadReg8(sn, Sn_CR) && --timeout) ;   /* 等待硬件清零 */
```

非法命令同样会被硬件清零，于是等待顺利结束，函数返回"成功"。
日志里因此只剩一条 `LISTEN fail`，**没有任何"命令超时"提示**，
把排查方向错误地引向了 SR 读取时序、SPI 相位、socket 复用、硬件信号完整性等方向。

一句话：**"CR 被清零"不等于"命令执行成功"，必须校验 SR 的状态迁移。**

### 3.6 关于另外几个 socket 的报错

日志里 sn1 / sn2 / sn4 还有 `cmd 0x10 超时 (CR=0x10)`、`cmd 0x01 超时 (CR=0x01)`、
`OPEN fail: SR=0x81 / 0x04 / 0x00`。这些属于同一初始化序列中的连带现象
（socket 0 的 LISTEN 反复失败并在超时后继续轮询，压缩了其它 socket 的初始化窗口），
修复 LISTEN 后重烧固件不再复现。若后续单独出现，需按单个 socket 独立排查
（常见原因：上一个连接未彻底 CLOSE、SR 读取过早、多 socket 共享缓冲分配不足）。

## 4. 修复方案

原则：**命令码与状态值彻底分家，用前缀区分**，杜绝同名覆盖。

| 位置 | 修改前 | 修改后 |
|---|---|---|
| W5500.H:39 | `#define SOCK_OPEN 0x01` | `#define W5500_CMD_OPEN 0x01` |
| W5500.H:40 | `#define SOCK_LISTEN 0x02` | `#define W5500_CMD_LISTEN 0x02` |
| W5500.H:41 | `#define SOCK_CONNECT 0x04` | `#define W5500_CMD_CONNECT 0x04` |
| W5500.H:42 | `#define SOCK_DISCON 0x08` | `#define W5500_CMD_DISCON 0x08` |
| W5500.H:43 | `#define SOCK_CLOSE 0x10` | `#define W5500_CMD_CLOSE 0x10` |
| W5500.H:44 | `#define SOCK_SEND 0x20` | `#define W5500_CMD_SEND 0x20` |
| W5500.H:45 | `#define SOCK_RECV 0x40` | `#define W5500_CMD_RECV 0x40` |
| W5500.H:47-51 | `SOCK_CLOSED/INIT/LISTEN/ESTABLISHED/CLOSE_WAIT` | 保持不变（这是 SR 状态值，语义正确） |

调用点同步修改：

- W5500.c:215（TcpOpen 里的 CLOSE）→ `W5500_CMD_CLOSE`
- W5500.c:233（OPEN）→ `W5500_CMD_OPEN`
- W5500.c:245（LISTEN）→ `W5500_CMD_LISTEN`
- W5500.c:246（等待状态）→ 仍用状态宏 `SOCK_LISTEN`（0x14）
- W5500.c:306（CloseAllSockets）→ `W5500_CMD_CLOSE`
- W5500.c:370（DISCON）→ `W5500_CMD_DISCON`

命名规范（建议全工程遵守）：

- 凡是写进 Sn_CR 的命令码：`W5500_CMD_xxx`
- 凡是 Sn_SR 的状态值：`SOCK_xxx`（或 `W5500_SR_xxx`）
- 凡是寄存器地址：`Sn_xxx` / `W5500_xxx`，BSB 用函数式宏 `W5500_BSB_TXBUF(n)`

备份文件：`W5500.H.bak_fix`、`W5500.c.bak_fix`

## 5. 修复后的验证

### 5.1 服务器就绪（22:51:40）

```
[W5500] VERSIONR=0x04 (期望 0x04)
[NET] 8 个 socket 已全部关闭
[W5500] IP = 192.168.1.200
[W5500] PHYCFGR=0xBF  LINK=UP
[W5500] PHYCFGR=0xBF LINK 100M Full
[NET] socket 0 LISTEN 成功 (端口 5000)
[NET] sn=0 Sn_SR=0x14
```

### 5.2 建立连接 + 双向收发（22:58，PC 用网络调试助手做客户端）

网络调试助手侧：

```
22:58:34.350  Server connected from local 192.168.1.100:35150
22:58:35.039  SEND ASCII/13 >>>  test-echo-123
22:58:35.089  RECV ASCII/13 from 192.168.1.200:5000 <<<  test-echo-123
22:58:36.787  SEND ASCII/13 >>>  test-echo-123
22:58:36.815  RECV ASCII/13 from 192.168.1.200:5000 <<<  test-echo-123
状态栏：TX:26  RX:26
```

模块侧串口：

```
22:58:34.385  [NET] sn=0 Sn_SR=0x17      <- 连接建立
22:58:35.091  [NET] sn0 echo 13 bytes    <- 回显成功
22:58:36.849  [NET] sn0 echo 13 bytes    <- 第二次同样成功
```

### 5.3 全链路状态

| 环节 | 判据 | 结果 |
|---|---|---|
| SPI 读写寄存器 | VERSIONR = 0x04 | 通过 |
| PHY 链路 | PHYCFGR = 0xBF，100M Full | 通过 |
| OPEN | SR = 0x13 (SOCK_INIT) | 通过 |
| LISTEN | SR = 0x14，日志 "LISTEN 成功" | 通过 |
| 三次握手 | SR = 0x17 (SOCK_ESTABLISHED) | 通过 |
| RX 缓冲读取 | Sn_RX_RSR / Sn_RX_RD 指针推进，读出 13 字节 | 通过 |
| TX 缓冲写入 + SEND | Sn_TX_WR 推进，PC 端收到 13 字节 | 通过 |

## 6. 经验与预防措施

1. **寄存器常量必须分类加前缀。** W5500 的命令码与状态值数值区间不同但语义相近，
   共处一个头文件时极易撞名。命令用 `W5500_CMD_*`，状态用 `SOCK_*`，永不复用同一个名字。
2. **把编译警告当错误处理。** 本次宏重定义只触发 `#47-D` warning。建议开 `--strict`
   或定期清理 warning 列表，尤其 `#47-D`（宏重定义）、`#177-D`（未使用变量）这类。
3. **命令成功判据要落在状态上，不能只看 CR 清零。** `w5500_cmd()` 写完 CR 后应当
   校验目标状态（例如 LISTEN 后必须在超时内看到 SR=0x14），否则非法命令会被静默吞掉。
4. **调试打印要一次打全。** 本次能定位，靠的就是那行同时打印了
   `MR / CR / SR / PORT / IR / RXBUF / TXBUF` 的日志。打印字段越全，定位越快。
5. **读通路正常 ≠ 写通路正常。** VERSIONR 读得对只证明读方向没问题，
   写方向的错误（命令码非法）不会被它发现。
6. **确认烧进去的是新固件。** 在调试打印里加一个"特征字段"（本次是 `MSS=%u`），
   每次改完代码先看这个字段是否变了，避免对着旧固件调新代码。
7. **对照"已知可用"的参考实现，比自己推演更快。** 本次最终是靠逐行对照
   卖家提供的可运行例程才锁定宏重名的；前面对 OVR、信号完整性、socket 卡死的猜测全部落空，
   及早做参考实现对照能省下大量时间。

## 7. 附录

### A. Sn_SR 状态码表

| 状态 | 值 | 含义 |
|---|---|---|
| SOCK_CLOSED | 0x00 | 未打开 |
| SOCK_INIT | 0x13 | TCP 已初始化（OPEN 完成） |
| SOCK_LISTEN | 0x14 | TCP 服务器监听中 |
| SOCK_SYNSENT | 0x15 | 已发 SYN（客户端） |
| SOCK_ESTABLISHED | 0x17 | 连接已建立 |
| SOCK_CLOSE_WAIT | 0x1C | 对端已发 FIN |
| SOCK_CLOSING | 0x20 | 关闭中 |
| SOCK_TIMEWAIT | 0x21 | 等待 2MSL |
| SOCK_UDP | 0x22 | UDP 模式 |
| SOCK_CLOSED_IPRAW | 0x32 | 无 IP 的 UDP |

### B. 排查 checklist（同类"命令不生效"故障通用）

1. 目标状态是多少？当前 SR 是多少？（先确认"卡在哪一步"）
2. CR 写完是否被清零？清零了但状态没变 → 优先怀疑命令码非法 / 前置条件不满足。
3. 命令码、状态码是否与其他宏撞名？（`grep -n` 整个宏名，看是否出现两次）
4. 目标 socket 是否处于 CLOSE 状态？缓冲分配是否足够（Sn_RXBUF_SIZE / Sn_TXBUF_SIZE）？
5. MR / PORT / MSSR 等前置寄存器是否写成功（读回校验）？
6. 固件是不是最新的（看打印特征字段）？
