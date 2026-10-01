package kanban

import "math/bits"

// qtRandom is a 32-bit Mersenne Twister (MT19937) seeded as the C++
// standard library's std::seed_seq seeds one from a single 32-bit value.
// TestMutationPreservationProperty draws its 300 boards from it. It gives
// the same numbers as the random generator of the earlier Qt version of Kvit
// Notes, so the test builds the boards that version's test built.
type qtRandom struct {
	state [624]uint32
	next  int
}

func newQtRandom(seed uint32) *qtRandom {
	const n = 624
	r := &qtRandom{next: n}

	// std::seed_seq::generate over 624 words for the one-value sequence
	// {seed}, as the C++ standard specifies it ([rand.util.seedseq]).
	a := &r.state
	for i := range a {
		a[i] = 0x8b8b8b8b
	}
	const s = 1
	const t = 11 // n >= 623
	const p = (n - t) / 2
	const q = p + t
	m := max(s+1, n)
	mix := func(x uint32) uint32 { return x ^ (x >> 27) }
	for k := 0; k < m; k++ {
		r1 := 1664525 * mix(a[k%n]^a[(k+p)%n]^a[(k+n-1)%n])
		var r2 uint32
		switch {
		case k == 0:
			r2 = r1 + s
		case k <= s:
			r2 = r1 + uint32(k%n) + seed
		default:
			r2 = r1 + uint32(k%n)
		}
		a[(k+p)%n] += r1
		a[(k+q)%n] += r2
		a[k%n] = r2
	}
	for k := m; k < m+n; k++ {
		r3 := 1566083941 * mix(a[k%n]+a[(k+p)%n]+a[(k+n-1)%n])
		r4 := r3 - uint32(k%n)
		a[(k+p)%n] ^= r3
		a[(k+q)%n] ^= r4
		a[k%n] = r4
	}
	// mersenne_twister_engine::seed(seed_seq&): a state of all zeros except
	// for the bits the recurrence ignores is replaced by one that works.
	zero := a[0]&0x80000000 == 0
	for i := 1; i < n && zero; i++ {
		zero = a[i] == 0
	}
	if zero {
		a[0] = 0x80000000
	}
	return r
}

// generate returns the next MT19937 output.
func (r *qtRandom) generate() uint32 {
	const n, m = 624, 397
	if r.next >= n {
		for i := 0; i < n; i++ {
			y := r.state[i]&0x80000000 | r.state[(i+1)%n]&0x7fffffff
			v := r.state[(i+m)%n] ^ y>>1
			if y&1 != 0 {
				v ^= 0x9908b0df
			}
			r.state[i] = v
		}
		r.next = 0
	}
	y := r.state[r.next]
	r.next++
	y ^= y >> 11
	y ^= y << 7 & 0x9d2c5680
	y ^= y << 15 & 0xefc60000
	y ^= y >> 18
	return y
}

// bounded is a value in [0, highest) scaled from one 32-bit output.
func (r *qtRandom) bounded(highest int) int {
	return int(uint64(r.generate()) * uint64(highest) >> 32)
}

// bounded64 is a value in [0, highest), the way the test picks an index
// into a list: 64-bit outputs masked to the bits highest needs, drawn again
// until one is below highest.
func (r *qtRandom) bounded64(highest int) int {
	width := bits.LeadingZeros64(uint64(highest - 1))
	mask := uint64(1)<<(64-width) - 1
	for {
		lo := uint64(r.generate())
		hi := uint64(r.generate())
		if v := (lo | hi<<32) & mask; v < uint64(highest) {
			return int(v)
		}
	}
}
