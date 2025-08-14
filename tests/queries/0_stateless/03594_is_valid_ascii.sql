-- constant input
select 1 = isValidASCII('');
select 1 = isValidASCII('some text');
select 0 = isValidASCII('какой-то текст');
select 1 = isValidASCII('\x00');
select 1 = isValidASCII('\x7F');
select 0 = isValidASCII('\x80');
select 1 = isValidASCII('\x00\x7F');
select 1 = isValidASCII('\x7F\x00');
select 0 = isValidASCII('\xC2\x80');
select 0 = isValidASCII('\x7F\x80');
select 0 = isValidASCII('\x70\x70\x80');
select 0 = isValidUTF8('\x66\x80\x00');
select 1 = isValidASCII(repeat('\x7F\x00', 200));
select 0 = isValidASCII(repeat('\x7F\x80', 200));

-- fixed size constant input
select 1 = isValidASCII(toFixedString('', 1));
select 1 = isValidASCII(toFixedString('some text', 9));
select 0 = isValidASCII(toFixedString('какой-то текст', 26));
select 1 = isValidASCII(toFixedString('\x00', 1));
select 1 = isValidASCII(toFixedString('\x7F', 1));
select 0 = isValidASCII(toFixedString('\x80', 1));
select 1 = isValidASCII(toFixedString('\x00\x7F', 2));
select 1 = isValidASCII(toFixedString('\x7F\x00', 2));
select 0 = isValidASCII(toFixedString('\xC2\x80', 2));
select 0 = isValidASCII(toFixedString('\x7F\x80', 2));
select 0 = isValidASCII(toFixedString('\x70\x70\x80', 3));
select 0 = isValidUTF8(toFixedString('\x66\x80\x00', 3));
select 1 = isValidASCII(toFixedString(repeat('\x7F\x00', 200), 400));
select 0 = isValidASCII(toFixedString(repeat('\x7F\x80', 200), 400));

-- alias
select 1 = isASCII('some text');
select 1 = isASCII(toFixedString('some text', 9));

-- column input
DROP TABLE IF EXISTS asciis;

CREATE TABLE asciis (val String) ENGINE = SummingMergeTree ORDER BY val;

INSERT INTO asciis (val) VALUES ('');
INSERT INTO asciis (val) VALUES ('some text');
INSERT INTO asciis (val) VALUES ('какой-то текст');
INSERT INTO asciis (val) VALUES ('\x00');
INSERT INTO asciis (val) VALUES ('\x7F'); 
INSERT INTO asciis (val) VALUES ('\x80'); 
INSERT INTO asciis (val) VALUES ('\x00\x7F'); 
INSERT INTO asciis (val) VALUES ('\x7F\x00'); 
INSERT INTO asciis (val) VALUES ('\xC2\x80'); 
INSERT INTO asciis (val) VALUES ('\x7F\x80'); 
INSERT INTO asciis (val) VALUES ('\x70\x70\x80');
INSERT INTO asciis (val) VALUES ('\x66\x80\x00');

SELECT isValidASCII(val) FROM asciis ORDER BY val;

DROP TABLE asciis;

-- unsupported arguments
select isValidUTF8(['\x00', '\x7F']); -- { serverError ILLEGAL_TYPE_OF_ARGUMENT }
SELECT isValidUTF8(toUUID('00000000-0000-0000-0000-000000000000')); -- { serverError ILLEGAL_TYPE_OF_ARGUMENT }
SELECT isValidUTF8(toIPv6('127.0.0.1')); -- { serverError ILLEGAL_TYPE_OF_ARGUMENT }
select isValidUTF8(toIPv4('127.0.0.1')); -- { serverError ILLEGAL_TYPE_OF_ARGUMENT }
