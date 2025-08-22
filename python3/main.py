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
    snapshot1 = [0, 0, 0, 0, 0]
    snapshot2 = [0, 0, 0, 0, 0]
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

                    # どのエンコーダ向けか判定
                    target = None  # 1 or 2
                    # 先頭に E1_/E2_ や L_ / R_ が付いている場合や left/right を含む場合に対応
                    if kl.startswith('e1') or kl.startswith('l') or 'left' in kl:
                        target = 1
                    elif kl.startswith('e2') or kl.startswith('r') or 'right' in kl:
                        target = 2

                    # プレフィックスを取り除いて実際のキー名を取り出す
                    # 例: e1_count_total -> count_total, l_rad -> rad
                    norm = kl
                    if '_' in norm:
                        parts = norm.split('_', 1)
                        if parts[0] in ('e1', 'e2', 'l', 'r'):
                            norm = parts[1]
                    else:
                        # e1count_total のようなケースを少し寛容に処理
                        if norm.startswith('e1'):
                            norm = norm[2:].lstrip('_')
                        elif norm.startswith('e2'):
                            norm = norm[2:].lstrip('_')
                        elif norm.startswith('l') and len(norm) > 1:
                            norm = norm[1:].lstrip('_')
                        elif norm.startswith('r') and len(norm) > 1:
                            norm = norm[1:].lstrip('_')

                    try:
                        # デフォルトは target が決まっていなければ既存の snapshot を更新（互換性）
                        if target is None:
                            # 既存のシングルエンコーダ用フィールド名を優先して扱う
                            if 'count_total' in norm:
                                snapshot[0] = int(float(val_str))
                                print('Stored to default snapshot count_total')
                            elif 'rad/s' in kl or 'ω' in key or 'omega' in norm:
                                snapshot[2] = float(val_str)
                            elif norm.startswith('rad'):
                                snapshot[1] = float(val_str)
                            elif 'rps' in norm:
                                snapshot[3] = float(val_str)
                            elif 'rpm' in norm:
                                snapshot[4] = float(val_str)
                            else:
                                continue
                        else:
                            # ターゲットごとのスナップショットに格納
                            dst = snapshot1 if target == 1 else snapshot2
                            if 'count_total' in norm:
                                dst[0] = int(float(val_str))
                            elif 'rad/s' in kl or 'ω' in key or 'omega' in norm:
                                dst[2] = float(val_str)
                            elif norm.startswith('rad'):
                                dst[1] = float(val_str)
                            elif 'rps' in norm:
                                dst[3] = float(val_str)
                            elif 'rpm' in norm:
                                dst[4] = float(val_str)
                            else:
                                continue
                    except Exception as e:
                        print('Parse value error:', e)
                        continue

                    # 現在のスナップショットを表示
                    # 両方のエンコーダ状態を表示
                    print('E1:', {labels[i]: snapshot1[i] for i in range(len(labels))})
                    print('E2:', {labels[i]: snapshot2[i] for i in range(len(labels))})
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