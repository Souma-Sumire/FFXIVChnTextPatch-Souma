import os
import argparse
from opencc import OpenCC
import time
from concurrent.futures import ProcessPoolExecutor, as_completed
from tqdm import tqdm

cc = None

def init_opencc():
    global cc
    cc = OpenCC('s2tw')

def convert_file(file_path):
    global cc
    temp_file = file_path + '.tmp'
    try:
        with open(file_path, 'r', encoding='utf-8-sig') as f:
            content = f.read()
        converted = cc.convert(content)
        with open(temp_file, 'w', encoding='utf-8-sig') as f:
            f.write(converted)
        os.replace(temp_file, file_path)
        return True, file_path
    except Exception as e:
        if os.path.exists(temp_file):
            os.remove(temp_file)
        return False, f"{file_path} -> {e}"

def main():
    parser = argparse.ArgumentParser(description='CSV 簡體轉臺灣繁體工具')
    parser.add_argument('-d', '--directory', default='resource/rawexd',
                        help='要處理的資料夾路徑')
    parser.add_argument('-p', '--processes', type=int, default=os.cpu_count(),
                        help='並行處理的進程數')
    args = parser.parse_args()

    # 確認提示
    print(f"準備開始轉換目錄：{os.path.abspath(args.directory)}")
    confirm = input("是否繼續？輸入 Y 確認，其他鍵取消：")
    if confirm.strip().lower() != 'y':
        print("操作已取消。")
        return

    if not os.path.exists(args.directory):
        print(f"錯誤：目錄不存在 {os.path.abspath(args.directory)}")
        return

    file_list = [os.path.join(root, f)
                 for root, _, files in os.walk(args.directory)
                 for f in files if f.lower().endswith('.csv')]

    print(f"共找到 {len(file_list)} 個 CSV 檔案 | 使用 {args.processes} 個處理程序")
    start = time.time()

    with ProcessPoolExecutor(max_workers=args.processes, initializer=init_opencc) as executor:
        futures = {executor.submit(convert_file, f): f for f in file_list}
        results = []
        for future in tqdm(as_completed(futures), total=len(futures)):
            results.append(future.result())

    total_time = time.time() - start
    print(f"\n轉換完成 | 耗時：{total_time:.1f} 秒 | 速度：{len(file_list)/total_time:.1f} 檔案/秒")
    for status, msg in results:
        if not status:
            print(f"× 轉換失敗：{msg}")

    input("\n請按任意鍵結束...")

if __name__ == '__main__':
    from multiprocessing import freeze_support
    freeze_support()
    main()
