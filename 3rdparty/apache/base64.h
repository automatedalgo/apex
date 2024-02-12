/* Base64 encoder/decoder. Originally Apache file ap_base64.h. Modified to
 * remove references to EBDIC.
 */

/* ====================================================================
 * The Apache Software License, Version 1.1
 *
 * Copyright (c) 2000 The Apache Software Foundation.  All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The end-user documentation included with the redistribution,
 *    if any, must include the following acknowledgment:
 *       "This product includes software developed by the
 *        Apache Software Foundation (http://www.apache.org/)."
 *    Alternately, this acknowledgment may appear in the software itself,
 *    if and wherever such third-party acknowledgments normally appear.
 *
 * 4. The names "Apache" and "Apache Software Foundation" must
 *    not be used to endorse or promote products derived from this
 *    software without prior written permission. For written
 *    permission, please contact apache@apache.org.
 *
 * 5. Products derived from this software may not be called "Apache",
 *    nor may "Apache" appear in their name, without prior written
 *    permission of the Apache Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE APACHE SOFTWARE FOUNDATION OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * ====================================================================
 *
 * This software consists of voluntary contributions made by many
 * individuals on behalf of the Apache Software Foundation.  For more
 * information on the Apache Software Foundation, please see
 * <http://www.apache.org/>.
 *
 * The apr_vsnprintf/apr_snprintf functions are based on, and used with the
 * permission of, the  SIO stdio-replacement strx_* functions by Panos
 * Tsirigotis <panos@alumni.cs.colorado.edu> for xinetd.
 */

#ifndef APACHE_BASE64_H
#define APACHE_BASE64_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @package Base64 Encoding
 */

/* Simple BASE64 encode/decode functions.
 *
 * As we might encode binary strings, hence we require the length of
 * the incoming plain source. And return the length of what we decoded.
 *
 * The decoding function takes any non valid char (i.e. whitespace, \0
 * or anything non A-Z,0-9 etc as terminal.
 *
 * plain strings/binary sequences are not assumed '\0' terminated. Encoded
 * strings are neither. But probably should.
 *
 */

/**
 * Given the length of an un-encrypted string, get the length of the encrypted string.
 * @param the length of an unencrypted string.
 * @return the length of the string after it is encrypted
 * @deffunc int ap_base64encode_len(int len)
 */
  int ap_base64encode_len(int len);

/**
 * Encode a text string using base64encoding.
 * @param The destination string for the encoded string.
 * @param The original string in plain text
 * @param The length of the plain text string
 * @return the length of the encoded string
 * @deffunc int ap_base64encode(char *coded_dst, const char *plain_src, int len_plain_src)
 */
int ap_base64encode(char * coded_dst, const char *plain_src,int len_plain_src);

/**
 * Determine the length of a plain text string given the encoded version
 * @param The encoded string
 * @return the length of the plain text string
 * @deffunc int ap_base64decode_len(const char *coded_src)
 */
  int ap_base64decode_len(const char * coded_src);

/**
 * Decode a string to plain text
 * @param The destination string for the plain text
 * @param The encoded string
 * @return the length of the plain text string
 * @deffunc int ap_base64decode(char *plain_dst, const char *coded_src)
 */
  int ap_base64decode(char * plain_dst, const char *coded_src);

/**
 * Decode an EBCDIC string to plain text
 * @param The destination string for the plain text
 * @param The encoded string
 * @return the length of the plain text string
 * @deffunc int ap_base64decode_binary(char *plain_dst, const char *coded_src)
 */
  int ap_base64decode_binary(unsigned char * plain_dst, const char *coded_src);

#ifdef __cplusplus
}
#endif

#endif	/* !APACHE_BASE64_H */
