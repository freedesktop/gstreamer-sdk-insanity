# GStreamer QA system
#
#       generators/constant.py
#
# Copyright (c) 2012, Collabora Ltd <sebastian.droege@collabora.co.uk>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 02111-1307, USA.

"""
Constant generator
"""

from insanity.generator import Generator
from insanity.log import debug, info, exception

class ConstantGenerator(Generator):
    """
    Arguments:
    * a constant string

    Returns:
    * the constant string
    """

    __args__ = {
        "constant":"The constant string",
        }

    # We don't know any semantics, derived classes are welcome to add some
    __produces__ = None

    def __init__(self, constant="", *args, **kwargs):
        """
        constant: A constant string
        """
        Generator.__init__(self, *args, **kwargs)
        self.constant = constant
        info("constant:%r" % (self.constant))

    def _generate(self):
        info("Returning %r" % (self.constant))
        return [self.constant]

