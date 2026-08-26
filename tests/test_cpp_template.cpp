// SPDX-License-Identifier: GPL-2.0-or-later

template <class T> struct S {
	T value;
	long use(T v);
};

template struct S<int>;
template struct S<long>;

template <> struct S<const char *> {
	const char *value;
};

struct Outer {
	S<long> sp;
	S<int> si;
};

long sum_field(Outer o)
{
	return o.sp.value + o.si.value;
}

int main(void)
{
	S<long> s = { 4L };
	Outer o = { S<long>{ 1L }, S<int>{ 2 } };
	S<const char *> sp = { "x" };
	S<int> si = { 3 };

	long r = s.use(5L);
	o.sp.value = 9L;
	si.value = 7;

	return (int)r;
}
