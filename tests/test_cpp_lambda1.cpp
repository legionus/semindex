// SPDX-License-Identifier: GPL-2.0-or-later

auto file_lambda = [](int v) { return v * 2; };

int double_it(int v)
{
	auto local_lambda = [v](int x) { return v + x; };
	return local_lambda(1);
}

int main()
{
	return file_lambda(1) + double_it(2);
}
