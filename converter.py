import os
import argparse
from opencc import OpenCC
import time
from concurrent.futures import ProcessPoolExecutor
from tqdm import tqdm
from concurrent.futures import as_completed

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
    parser = argparse.ArgumentParser(description='CSV 转台湾繁体工具')
    parser.add_argument('-d', '--directory', default='resource/rawexd',
                        help='要处理的根目录路径')
    parser.add_argument('-p', '--processes', type=int, default=os.cpu_count(),
                        help='并行进程数')
    args = parser.parse_args()

    if not os.path.exists(args.directory):
        print(f"错误：目录不存在 {os.path.abspath(args.directory)}")
        return

    file_list = [os.path.join(root, f)
                 for root, _, files in os.walk(args.directory)
                 for f in files if f.lower().endswith('.csv')]

    print(f"找到 {len(file_list)} 个 CSV 文件 | 使用 {args.processes} 个进程")
    start = time.time()

    with ProcessPoolExecutor(max_workers=args.processes, initializer=init_opencc) as executor:
        futures = {executor.submit(convert_file, f): f for f in file_list}
        results = []
        for future in tqdm(as_completed(futures), total=len(futures)):
            results.append(future.result())

    total_time = time.time() - start
    print(f"\n转换完成 | 耗时: {total_time:.1f}s | 速度: {len(file_list)/total_time:.1f} 文件/秒")
    for status, msg in results:
        if not status:
            print(f"× {msg}")

if __name__ == '__main__':
    main()
