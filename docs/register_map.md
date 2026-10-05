# Simulated Register Map / ioctl Interface
| Logical register | ioctl | Description |
|---|---|---|
| CTRL.START / STOP | `VS_IOC_START`, `VS_IOC_STOP` | Start/stop the hrtimer ("hardware clock") |
| SAMPLE_RATE | `VS_IOC_SET_RATE` (ms, 1-60000) | Timer period (applies at next start) |
| THRESHOLD | `VS_IOC_SET_THRESH` (milli-degC) | Sample above this sets alert flag and raises SIGIO |
| STATUS | `VS_IOC_GET_STATS` | produced / consumed / dropped / alerts counters |
| FIFO flush | `VS_IOC_CLEAR` | Empties ring buffer |

Sample format (`struct vs_sample`): `ts_ns` (u64), `value` (s32, milli-degC), `alert` (u32).
