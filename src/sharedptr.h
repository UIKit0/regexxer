/*
 * Copyright (c) 2002-2007  Daniel Elstner  <daniel.kitta@gmail.com>
 *
 * This file is part of regexxer.
 *
 * regexxer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * regexxer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with regexxer; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef REGEXXER_SHAREDPTR_H_INCLUDED
#define REGEXXER_SHAREDPTR_H_INCLUDED

#include <algorithm>

namespace Util
{

template <class> class SharedPtr;

/*
 * Common base class of objects managed by SharedPtr<>.
 */
class SharedObject
{
protected:
  inline SharedObject(); // initial reference count is 0
  inline ~SharedObject();

private:
  mutable long refcount_;

  // noncopyable
  SharedObject(const SharedObject&);
  SharedObject& operator=(const SharedObject&);

  template <class> friend class SharedPtr;
};

/*
 * Intrusive smart pointer implementation.  It requires the managed types
 * to be derived from class SharedObject, in order to do reference counting
 * as efficient as possible.
 *
 * The intrusive approach also simplifies the implementation, particularly
 * with regards to exception safety.  A non-intrusive smart pointer like
 * boost::shared_ptr<> would have to allocate memory to hold the reference
 * count -- this is tricky not only because of the 'new' which could throw,
 * but it also complicates the implementation of the cast templates like
 * shared_dynamic_cast<> etc.
 *
 * The cast template functions use the same syntax as in boost:
 *
 *     shared_static_cast<T>        returns static_cast<T*>(pointer)
 *     shared_dynamic_cast<T>       returns dynamic_cast<T*>(pointer)
 *     shared_polymorphic_cast<T>   returns &dynamic_cast<T&>(*pointer)
 *
 * I didn't implement shared_polymorphic_downcast<T> because it seems to be
 * just a debug check for those who don't want to ship with debugging enabled.
 * This would be silly IMHO, considering that the dynamic_cast<> overhead is
 * negligible in a GUI application like regexxer.
 *
 * About operator const void*() const:
 *
 * This operator fulfills the same task operator bool() would, but safer.
 * A SharedPtr<> will never be implicitely converted to an integer type,
 * which is particularly important in the context of overload resolution.
 * An additional advantage is that operator const void*() gets us equality
 * and non-equality tests for free.
 *
 * Note that boost is using an operator that converts to a PMF (pointer to
 * member function) for this purpose.  However, I consider this solution
 * to be somewhat over the top.
 */
template <class T>
class SharedPtr
{
public:
  inline SharedPtr();
  inline ~SharedPtr();

  explicit inline SharedPtr(T* ptr); // obtains reference

  inline SharedPtr(const SharedPtr<T>& other);
  inline SharedPtr<T>& operator=(const SharedPtr<T>& other);

  template <class U> inline SharedPtr(const SharedPtr<U>& other);
  template <class U> inline SharedPtr<T>& operator=(const SharedPtr<U>& other);

  inline void swap(SharedPtr<T>& other);

  inline void reset(T* ptr = 0); // obtains reference
  inline T*   get() const;

  inline T* operator->() const;
  inline T& operator*()  const;

  inline operator const void*() const;

private:
  T* ptr_;
};

/*
 * Explicitely forbid the usage of a generic SharedPtr<SharedObject>
 * because class SharedObject doesn't have a virtual destructor.
 */
template <> class SharedPtr<SharedObject> {};
template <> class SharedPtr<const SharedObject> {};

inline
SharedObject::SharedObject()
:
  refcount_ (0)
{}

inline
SharedObject::~SharedObject()
{}

template <class T> inline
SharedPtr<T>::SharedPtr()
:
  ptr_ (0)
{}

template <class T> inline
SharedPtr<T>::~SharedPtr()
{
  if (ptr_ && --ptr_->refcount_ <= 0)
    delete ptr_;
}

template <class T> inline
SharedPtr<T>::SharedPtr(T* ptr)
:
  ptr_ (ptr)
{
  if (ptr_)
    ++ptr_->refcount_;
}

/*
 * Note that reset() and get() are defined here and not in declaration order
 * on purpose -- defining them before their first use is required with some
 * compilers for for maximum inlining.
 */
template <class T> inline
void SharedPtr<T>::swap(SharedPtr<T>& other)
{
  std::swap(ptr_, other.ptr_);
}

template <class T> inline
void SharedPtr<T>::reset(T* ptr)
{
  // Note that SharedPtr<>::reset() obtains a reference.
  SharedPtr<T> temp (ptr);
  std::swap(ptr_, temp.ptr_);
}

template <class T> inline
T* SharedPtr<T>::get() const
{
  return ptr_;
}

template <class T> inline
SharedPtr<T>::SharedPtr(const SharedPtr<T>& other)
:
  ptr_ (other.ptr_)
{
  if (ptr_)
    ++ptr_->refcount_;
}

template <class T> inline
SharedPtr<T>& SharedPtr<T>::operator=(const SharedPtr<T>& other)
{
  this->reset(other.ptr_);
  return *this;
}

template <class T>
  template <class U>
inline
SharedPtr<T>::SharedPtr(const SharedPtr<U>& other)
:
  ptr_ (other.get())
{
  if (ptr_)
    ++ptr_->refcount_;
}

template <class T>
  template <class U>
inline
SharedPtr<T>& SharedPtr<T>::operator=(const SharedPtr<U>& other)
{
  this->reset(other.get());
  return *this;
}

template <class T> inline
T* SharedPtr<T>::operator->() const
{
  return ptr_;
}

template <class T> inline
T& SharedPtr<T>::operator*() const
{
  return *ptr_;
}

template <class T> inline
SharedPtr<T>::operator const void*() const
{
  return ptr_;
}

template <class T> inline
void swap(SharedPtr<T>& a, SharedPtr<T>& b)
{
  a.swap(b);
}

template <class T, class U> inline
SharedPtr<T> shared_static_cast(const SharedPtr<U>& other)
{
  return SharedPtr<T>(static_cast<T*>(other.get()));
}

template <class T, class U> inline
SharedPtr<T> shared_dynamic_cast(const SharedPtr<U>& other)
{
  return SharedPtr<T>(dynamic_cast<T*>(other.get()));
}

template <class T, class U> inline
SharedPtr<T> shared_polymorphic_cast(const SharedPtr<U>& other)
{
  return SharedPtr<T>(&dynamic_cast<T&>(*other)); // may throw std::bad_cast
}

} // namespace Util

#endif /* REGEXXER_SHAREDPTR_H_INCLUDED */
