package main

import (
	"bytes"
	"database/sql"
	"fmt"
	"io"
	"net"
	"os"
	"time"

	_ "github.com/lib/pq"
)

const (
	ColorGreen = "\033[32m"
	ColorRed   = "\033[31m"
	ColorReset = "\033[0m"
)

const (
	host   = "localhost" // pg host
	port   = 5432        // pg port
	dbname = "postgres"  // db name
)

const (
	simpleCreateQuery = `
		CREATE TABLE IF NOT EXISTS test (
			test1 TEXT,
			test2 TEXT,
			test3 TEXT,

			UNIQUE (test1, test2, test3)
		);`

	simpleDropQuery = `DROP TABLE IF EXISTS test;`
)

type Test struct {
	testName   string
	callback   func(test *Test) bool
	waitAnswer []byte
	realAnswer []byte
	err        string
}

type TestConn struct {
	connToPG    *sql.DB
	connToCache net.Conn
}

func (conn *TestConn) Close() {

	err := conn.connToCache.Close()
	if err != nil {
		fmt.Println("error cache drop exec: %w", err)
	}

	err = conn.connToPG.Close()
	if err != nil {
		fmt.Println("error postgres close: %w", err)
	}
}

func (conn *TestConn) Open() error {
	var err error

	conn.connToCache, err = net.Dial("tcp", "localhost:6379")
	if err != nil {
		return fmt.Errorf("error connect to localhost:6379: %w", err)
	}

	conn.connToCache.SetReadDeadline(time.Now().Add(2 * time.Second))

	psqlInfo := fmt.Sprintf("host=%s port=%d user=%s dbname=%s sslmode=disable",
		host, port, os.Getenv("USER"), dbname)

	conn.connToPG, err = sql.Open("postgres", psqlInfo)
	if err != nil {
		return fmt.Errorf("error connect to postgres: %w", err)
	}

	return nil
}

func simpleSetTest(test *Test) bool {
	var conn TestConn
	var err error
	test.waitAnswer = []byte("+OK\r\n")

	err = conn.Open()
	if err != nil {
		test.err = "create connection error(" + err.Error() + ")"
		return false
	}
	defer conn.Close()

	request := []byte("*3\r\n$3\r\nset\r\n$26\r\ntest.test1.simple_set_test\r\n$37\r\ntest1:simple_set_test.test2:6.test3:4\r\n")
	res := DoTest(conn.connToCache, test, request)
	if err != nil {
		test.err = "set error"
		return false
	}
	return res
}

func simpleGetTest(test *Test) bool {
	var conn TestConn

	err := conn.Open()
	if err != nil {
		test.err = "create connection error(" + err.Error() + ")"
		return false
	}
	defer conn.Close()
	request := []byte("*3\r\n$3\r\nset\r\n$26\r\ntest.test1.simple_get_test\r\n$37\r\ntest1:simple_get_test.test2:2.test3:3\r\n")
	test.waitAnswer = []byte("+OK\r\n")

	if !DoTest(conn.connToCache, test, request) {
		test.err = "set err"
		return false
	}

	test.waitAnswer = []byte("*1\r\n*3\r\n$15\r\nsimple_get_test\r\n$1\r\n2\r\n$1\r\n3\r\n")
	request = []byte("*2\r\n$3\r\nget\r\n$26\r\ntest.test1.simple_get_test\r\n")
	res := DoTest(conn.connToCache, test, request)

	if !res {
		test.err = "get err"
		conn.Close()
	}
	return res
}

func simpleDelTest(test *Test) bool {
	var conn TestConn

	err := conn.Open()
	if err != nil {
		test.err = "create connection error(" + err.Error() + ")"
		return false
	}
	defer conn.Close()

	request := []byte("*3\r\n$3\r\nset\r\n$26\r\ntest.test1.simple_del_test\r\n$37\r\ntest1:simple_del_test.test2:2.test3:3\r\n")
	test.waitAnswer = []byte("+OK\r\n")

	if !DoTest(conn.connToCache, test, request) {
		test.err = "set err"
		return false
	}

	test.waitAnswer = []byte(":1\r\n")
	request = []byte("*2\r\n$3\r\ndel\r\n$26\r\ntest.test1.simple_del_test\r\n")
	res := DoTest(conn.connToCache, test, request)

	if !res {
		test.err = "del err"
	}
	return res
}

func simpleDelTest_x2(test *Test) bool {
	if !simpleDelTest(test) {
		test.err = "first simple del err"
		return false
	}

	if !simpleDelTest(test) {
		test.err = "second simple del err"
		return false
	}

	return true
}

func simpleSetTest_x2(test *Test) bool {
	if !simpleSetTest(test) {
		test.err = "first simple del err"
		return false
	}

	if !simpleSetTest(test) {
		test.err = "second simple del err"
		return false
	}

	return true
}

func doubleDelTest(test *Test) bool {
	var conn TestConn

	err := conn.Open()
	if err != nil {
		test.err = "create connection error(" + err.Error() + ")"
		return false
	}
	defer conn.Close()
	request := []byte("*3\r\n$3\r\nset\r\n$26\r\ntest.test1.double_del_test\r\n$37\r\ntest1:double_del_test.test2:2.test3:3\r\n")
	test.waitAnswer = []byte("+OK\r\n")

	if !DoTest(conn.connToCache, test, request) {
		test.err = "set err"
		return false
	}

	test.waitAnswer = []byte(":1\r\n")
	request = []byte("*2\r\n$3\r\ndel\r\n$26\r\ntest.test1.double_del_test\r\n")
	if !DoTest(conn.connToCache, test, request) {
		test.err = "first det err"
		return false
	}

	test.waitAnswer = []byte(":0\r\n")
	request = []byte("*2\r\n$3\r\ndel\r\n$32\r\ntest.test1.simple_doubleDel_test\r\n")
	res := DoTest(conn.connToCache, test, request)
	if !res {
		test.err = "second del err "
	}
	return res
}

func getFromDb(test *Test) bool {
	var conn TestConn
	err := conn.Open()
	if err != nil {
		test.err = "create connection error(" + err.Error() + ")"
		return false
	}
	defer conn.Close()

	// insertQuery := `INSERT INTO test (test1, test2, test3) VALUES ('get_from_db', '2', '3')`
	// _, err = conn.connToPG.Exec(insertQuery)
	// if err != nil {
	// 	test.err = "can't connect to db (" + err.Error() + ")"

	// }

	test.waitAnswer = []byte("*1\r\n*3\r\n$11\r\nget_from_db\r\n$1\r\n2\r\n$1\r\n3\r\n")
	request := []byte("*2\r\n$3\r\nget\r\n$22\r\ntest.test1.get_from_db\r\n")
	res := DoTest(conn.connToCache, test, request)

	if !res {
		test.err = "get err"
	}
	return res
}

func DoTest(conn net.Conn, test *Test, request []byte) bool {
	count_write, err := conn.Write(request)
	if err != nil || count_write != len(request) {
		fmt.Println("Error sending request: ", err)
		return false
	}
	test.realAnswer = make([]byte, len(test.waitAnswer))

	count_read, err := io.ReadFull(conn, test.realAnswer)
	if err != nil || count_read != len(test.waitAnswer) {
		fmt.Println("Error sending request: ", err)
		return false
	}

	if bytes.Equal(test.realAnswer, test.waitAnswer) {
		return true
	}

	return false
}

func escapeSpecialChars(data []byte) string {
	result := ""
	for _, b := range data {
		switch b {
		case '\n':
			result += "\\n"
		case '\r':
			result += "\\r"
		default:
			result += string(b)
		}
	}
	return result
}

func main() {

	tests := []Test{
		{
			testName: "simple set test",
			callback: simpleSetTest,
		},

		{
			testName: "simple set test x2",
			callback: simpleSetTest_x2,
		},
		{
			testName: "simple get test",
			callback: simpleGetTest,
		},
		{
			testName: "simple del test",
			callback: simpleDelTest,
		},
		{
			testName: "double del test",
			callback: doubleDelTest,
		},
		{
			testName: "simple del test x2",
			callback: simpleDelTest_x2,
		},
		{
			testName: "get from Db",
			callback: getFromDb,
		},
	}

	for test_num, test := range tests {
		result := test.callback(&test)
		if result {
			fmt.Printf("%s%d) %s - OK%s\n", ColorGreen, test_num+1, test.testName, ColorReset)
		} else {
			fmt.Printf("%s%d) %s - ERR: wait answer is '%s', but real answer  '%s' error:(%s)%s\n",
				ColorRed, test_num+1, test.testName, escapeSpecialChars(test.waitAnswer),
				escapeSpecialChars(test.realAnswer), test.err, ColorReset)
		}
	}
}
