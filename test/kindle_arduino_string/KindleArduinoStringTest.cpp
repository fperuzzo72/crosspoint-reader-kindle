// These tests are not about String working. They are about String working the
// way ARDUINO's String works, because 176 call sites in the app tree were
// written against that behaviour and quietly depend on it.
//
// Every case below is a place where the obvious std::string implementation
// would compile, look correct, and be wrong at runtime.

#include <gtest/gtest.h>

#include "ArduinoCompat.h"

namespace {

TEST(ArduinoString, IndexOfReturnsMinusOneNotNpos) {
  // The trap: std::string::find returns npos, which is SIZE_MAX. Assigned to
  // the int that callers use, that is -1 by accident on most platforms and
  // works right up until someone compares it unsigned.
  const String s("hello world");
  EXPECT_EQ(s.indexOf(String("world")), 6);
  EXPECT_EQ(s.indexOf(String("absent")), -1);
  EXPECT_EQ(s.indexOf('o'), 4);
  EXPECT_EQ(s.indexOf('z'), -1);
  EXPECT_EQ(s.lastIndexOf('o'), 7);
  EXPECT_EQ(s.lastIndexOf('z'), -1);
}

TEST(ArduinoString, IndexOfBeyondEndIsMinusOneNotUndefined) {
  const String s("abc");
  EXPECT_EQ(s.indexOf(String("a"), 99), -1);
  EXPECT_EQ(s.indexOf('a', 99), -1);
}

TEST(ArduinoString, SubstringClampsInsteadOfThrowing) {
  // std::string::substr throws out_of_range here. Arduino returns what it can,
  // and the app tree calls substring() with computed indices that go past the
  // end on empty input.
  const String s("abcdef");
  EXPECT_EQ(s.substring(2).str(), "cdef");
  EXPECT_EQ(s.substring(99).str(), "");
  EXPECT_EQ(s.substring(2, 4).str(), "cd");
  EXPECT_EQ(s.substring(2, 99).str(), "cdef");
  EXPECT_EQ(s.substring(4, 2).str(), "");  // inverted range is empty, not an error
  EXPECT_EQ(s.substring(99, 200).str(), "");
}

TEST(ArduinoString, RemoveToleratesOutOfRange) {
  // 92 call sites. std::string::erase throws past the end.
  String s("abcdef");
  s.remove(3);
  EXPECT_EQ(s.str(), "abc");

  String t("abcdef");
  t.remove(99);  // must be a no-op, not a throw
  EXPECT_EQ(t.str(), "abcdef");

  String u("abcdef");
  u.remove(1, 2);
  EXPECT_EQ(u.str(), "adef");

  String v("abcdef");
  v.remove(4, 999);  // count past the end clamps
  EXPECT_EQ(v.str(), "abcd");
}

TEST(ArduinoString, ReplaceDoesNotLoopForeverWhenToContainsFrom) {
  // The classic: replacing "a" with "aa" by advancing past the match, not by
  // re-searching from the same spot.
  String s("banana");
  s.replace(String("a"), String("aa"));
  EXPECT_EQ(s.str(), "baanaanaa");

  // An empty needle must not spin.
  String t("abc");
  t.replace(String(""), String("x"));
  EXPECT_EQ(t.str(), "abc");
}

TEST(ArduinoString, CharAtPastEndIsNulNotUndefined) {
  const String s("ab");
  EXPECT_EQ(s.charAt(0), 'a');
  EXPECT_EQ(s.charAt(99), '\0');
  EXPECT_EQ(s[99], '\0');
}

TEST(ArduinoString, TrimStripsBothEndsAndLeavesTheMiddle) {
  String s("  a b  ");
  s.trim();
  EXPECT_EQ(s.str(), "a b");

  String allSpace("    ");
  allSpace.trim();
  EXPECT_EQ(allSpace.str(), "");
}

TEST(ArduinoString, StartsAndEndsWithHandleOverlongNeedles) {
  const String s("ab");
  EXPECT_TRUE(s.startsWith(String("a")));
  EXPECT_TRUE(s.endsWith(String("b")));
  EXPECT_FALSE(s.startsWith(String("abc")));  // must not read past the end
  EXPECT_FALSE(s.endsWith(String("abc")));
  EXPECT_TRUE(s.startsWith(String("")));
}

TEST(ArduinoString, NullCharPointerIsEmptyNotACrash) {
  const char* nothing = nullptr;
  const String s(nothing);
  EXPECT_TRUE(s.isEmpty());
  EXPECT_STREQ(s.c_str(), "");
}

TEST(ArduinoString, ConcatenationAndComparison) {
  String s("a");
  s += String("b");
  s += "c";
  s += 'd';
  EXPECT_EQ(s.str(), "abcd");
  EXPECT_EQ((String("x") + String("y")).str(), "xy");
  EXPECT_TRUE(String("a") == String("a"));
  EXPECT_TRUE(String("a") != String("b"));
  EXPECT_TRUE(String("a") < String("b"));
  EXPECT_TRUE(String("AbC").equalsIgnoreCase(String("aBc")));
  EXPECT_FALSE(String("ab").equalsIgnoreCase(String("abc")));
}

TEST(ArduinoString, ToIntParsesLeadingDigitsAndYieldsZeroOtherwise) {
  EXPECT_EQ(String("42").toInt(), 42);
  EXPECT_EQ(String("42abc").toInt(), 42);
  EXPECT_EQ(String("abc").toInt(), 0);
  EXPECT_EQ(String("-7").toInt(), -7);
}

}  // namespace
