#pragma once


template<typename T, int size>
struct Vector {
	Vector(T x, T y) : x(x), y(y) {};

	union {
		struct { T x, y; };
		std::array<T, size> data;
	};
};