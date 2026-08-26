// SPDX-License-Identifier: GPL-2.0-or-later

namespace alpha
{

struct Point {
	long x;
	long y;

	Point(long px, long py);
	long process(long v);
};

enum Color {
	RED,
	GREEN
};

} // namespace alpha

namespace alpha
{
namespace beta
{

class Node
{
public:
	long value;
};

} // namespace beta
} // namespace alpha

struct Outer {
	alpha::Point p;
	alpha::Color color;
};

int main(void)
{
	alpha::Point p(1, 2);
	Outer o = { alpha::Point(1, 2), alpha::RED };

	p.process(9);
	o.p.x = 3;
	o.color = alpha::GREEN;
	return p.x;
}
