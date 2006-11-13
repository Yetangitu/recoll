#ifndef _REFCNTR_H_
#define _REFCNTR_H_

// See Stroustrup C++ 3rd ed, p. 783
template <class X> class RefCntr {
  X   *rep;
  int *pcount;
 public:
  X * operator->() {return rep;}
  RefCntr() : rep(0), pcount(0) {}
  RefCntr(X *pp) : rep(pp), pcount(new int(1)) {}
  RefCntr(const RefCntr &r) :rep(r.rep), pcount(r.pcount) { (*pcount)++;}
  RefCntr& operator=(const RefCntr& r) {
    if (rep == r.rep) return *this;
    if (pcount && --(*pcount) == 0) {
      delete rep;
      delete pcount;
    }
    rep = r.rep;
    pcount = r.pcount;
    if (pcount)
	(*pcount)++;
    return  *this;
  }
  ~RefCntr() {if (--(*pcount) == 0) {delete rep;delete pcount;}}
  int getcnt() const {return *pcount;}
  const X * getptr() const {return rep;}
};


#endif /*_REFCNTR_H_ */
