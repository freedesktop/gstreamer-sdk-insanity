/* Insanity QA system

 Copyright (c) 2012, Collabora Ltd <vincent@collabora.co.uk>

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this program; if not, write to the
 Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 Boston, MA 02111-1307, USA.
*/

#include <stdio.h>
#include <glib.h>
#include <glib-object.h>
#include <insanity/insanity.h>

static gboolean
blank_test_setup (InsanityTest * test)
{
  (void) test;
  printf ("blank_test_setup\n");

  GValue value, zero = { 0 };
  value = zero;
  if (insanity_test_get_argument (test, "uri", &value)) {
    const char *uri = g_value_get_string (&value);
    printf ("uri: %s\n", uri);
    g_value_unset (&value);
  }

  value = zero;
  if (insanity_test_get_argument (test, "test-argument", &value)) {
    const char *ta = g_value_get_string (&value);
    printf ("test-argument: %s\n", ta);
    g_value_unset (&value);
  }

  const char *fn =
      insanity_test_get_output_filename (test, "dummy-output-file");
  printf ("fn: %s\n", fn);

  return TRUE;
}

static gboolean
blank_test_start (InsanityTest * test)
{
  (void) test;
  printf ("blank_test_start\n");
  return TRUE;
}

static void
blank_test_stop (InsanityTest * test)
{
  (void) test;
  printf ("blank_test_stop\n");
}

static void
blank_test_test (InsanityTest * test)
{
  insanity_test_done (test);
  printf ("blank_test_test\n");
}

int
main (int argc, char **argv)
{
  InsanityTest *test;
  gboolean ret;
  GValue def;

  g_type_init ();

  test =
      INSANITY_TEST (insanity_threaded_test_new ("blank-c-test",
          "Sample test that does nothing", NULL));
  insanity_test_add_checklist_item (test, "random-step",
      "Some random step, nothing much", NULL);
  insanity_test_add_checklist_item (test, "other-random-step",
      "Some random step, nothing much either", "error blah");
  insanity_test_add_argument (test, "uri", "URI description",
      "URI full description", G_TYPE_STRING, "http://127.0.0.1/");
  insanity_test_add_output_file (test, "dummy-output-file", "dummy output file");

  g_signal_connect_after (test, "setup", G_CALLBACK (&blank_test_setup), 0);
  g_signal_connect_after (test, "start", G_CALLBACK (&blank_test_start), 0);
  g_signal_connect (test, "stop", G_CALLBACK (&blank_test_stop), 0);
  g_signal_connect_after (test, "test", G_CALLBACK (&blank_test_test), 0);


  ret = insanity_test_run (test, argc, argv);

  g_object_unref (test);

  return ret ? 0 : 1;
}
