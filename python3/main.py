import sys
import time
import serial

DEFAULT_PORT = '/dev/ttyACM0'
BAUD = 115200

def main():
    port = DEFAULT_PORT
    print(f"Opening{port} at {BAUD}bps. Ctrl-C to exit.")

    # 最新データを1つにまとめるリスト
    # [count_total(int), rad(float), omega(rad/s)(float), rps(float), rpm(float)]
    snapshot = [0, 0, 0, 0, 0]
    labels = ['count_total', 'rad', 'omega', 'rps', 'rpm']

    try:
        with serial.Serial(port, baudrate=BAUD, timeout=1) as ser:
            time.sleep(0.1)
            while True:
                try:
                    line = ser.readline()
                    if not line:
                        continue

                    # デコードして表示
                    try:
                        text = line.decode('utf-8', errors='replace').rstrip('\r\n')
                    except Exception:
                        text = str(line)
                    # print(text)

                    # "key: value" 形式をパース
                    s = text.lstrip('>').strip()
                    if ':' not in s:
                        continue
                    key, val_str = s.split(':', 1)
                    key = key.strip()
                    val_str = val_str.strip()

                    kl = key.lower()

                    try:
                        if 'count_total' in kl:
                            val = int(float(val_str))
                            snapshot[0] = val
                        elif 'rad/s' in kl or 'ω' in key or 'omega' in kl:
                            snapshot[2] = float(val_str)
                        elif kl.startswith('rad'):
                            snapshot[1] = float(val_str)
                        elif 'rps' in kl:
                            snapshot[3] = float(val_str)
                        elif 'rpm' in kl:
                            snapshot[4] = float(val_str)
                        else:
                            # 未知キーは無視
                            continue
                    except Exception as e:
                        print('Parse value error:', e)
                        continue

                    # 現在のスナップショットを表示
                    print('DATA:', {labels[i]: snapshot[i] for i in range(len(labels))})
                    print()

                except KeyboardInterrupt:
                    print('\nExiting.')
                    break
                except Exception as e:
                    print('Read error:', e)
                    break
    except serial.SerialException as e:
        print('Serial error:', e)

if __name__ == '__main__':
    main()