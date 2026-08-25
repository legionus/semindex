// SPDX-License-Identifier: GPL-2.0-or-later

class Greeting
{
public:
	int value;

	Greeting(int initial);
	int value_plus(int amount);
};

int main()
{
	Greeting greeting(10);
	return greeting.value_plus(5);
}
