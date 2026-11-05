#include <iostream>

#include <xivres/util.thread_pool.h>

using namespace xivres::util::thread_pool;

int main() {
	using namespace std::chrono_literals;

	pool pool(1);

	{
		task_waiter<int> waiter(pool);

		for (int i = 0; i < 100; i++) {
			waiter += pool.submit<int>([i, &pool](task<int>& t) {
				t.throw_if_cancelled();

				task_waiter<void> waiter2(pool);

				for (int j = 0; j < 1000; j++) {
					t.throw_if_cancelled();

					waiter2 += pool.submit<void>([i, j](task<void>& t) {
						for (size_t k = 0; k < 32768; k++)
							t.throw_if_cancelled();
						std::cout << i << "." << j << std::endl;
					});
				}

				while (waiter2.get())
					t.throw_if_cancelled();

				return i;
			});
		}

		std::this_thread::sleep_for(100ms);
	}
	std::cout << "end" << std::endl;

	return 0;
}