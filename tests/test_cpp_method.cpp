// SPDX-License-Identifier: GPL-2.0-or-later

struct Point {
	long x;
	long y;

	Point(long px, long py);
	long process(long v);
};

class Circle
{
public:
	long radius;

	Circle(long r);
	long process(long v);
};

int main()
{
	Point p(1, 2);
	Circle c(3);
	c.process(7);
	return p.process(9);
}
