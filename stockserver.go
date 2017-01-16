package main

import (
	"fmt"
	"net"
	"encoding/json"
	"time"
	"math/rand"
	"math"
)

type Request struct {
	Ticker string
	From   int64
}

type Response struct {
	Tstamp  []int64
	Price   []float64
	Volume  []int64
}

var reqchan  = make(chan *Request)
var respchan = make(chan *Response)

func main() {

	ln, err := net.Listen("tcp", ":1239")
	if err != nil {
		fmt.Println("Cannot listen to 1239 : err =", err)
		return
	}

	go genStats()

	for {
		conn, err := ln.Accept()
		if err != nil {
			// handle error
			fmt.Println("Error accepting connection : err =", err )
		}
		go handleConnection(conn)
	}
}


func handleConnection(conn net.Conn) {

	dec := json.NewDecoder(conn)
	req := Request{}
	buf := make([]byte, 4)
	
	for{ 
		err := dec.Decode(&req)
		if err != nil {
			fmt.Println("Error decoding request : err =", err)
			fmt.Println("Closing connection...")
			conn.Close()
			return
		}

		fmt.Println("Got request for", req.Ticker)
		reqchan <- &req
		resp := <-respchan
		content, err := json.Marshal(resp)
		length := uint64(len(content))
		fmt.Println("Generated", len(resp.Price), "entries", ", outlength =", length)
		buf[0] = byte(length & 0xff)
		buf[1] = byte((length >> 8) & 0xff)
		buf[2] = byte((length >> 16) & 0xff)
		buf[3] = byte((length >> 24) & 0xff)
		//fmt.Println("buf = ", buf, "content[0:20] =", content[0:20])
		conn.Write(buf)
		conn.Write(content)
	}
}


func genStats() {

	buflen  := 100
	Tstamp  := make([]int64,   buflen)
	Price   := make([]float64, buflen)
	Volume  := make([]int64,   buflen)

	beg := 0
	end := 0

	pr  := float64(550.23)
	vol := int64(25341)
	for {
		Tstamp[end] = time.Now().Unix() 
		Price[end]  = pr
		Volume[end] = vol
		delta1 := rand.NormFloat64()*0.1
		delta2 := rand.NormFloat64()*0.1
		delta1 = math.Min(0.15, delta1);
		delta2 = math.Min(0.15, delta2)
		delta1 = math.Max(-0.15, delta1);
		delta2 = math.Max(-0.15, delta2)
		
		pr += delta1
		vol = int64(float64(vol)*(1 + delta2))
		
		time.Sleep(500*time.Millisecond)

		select {
		case req := <-reqchan:
			resp := genResp(req, Tstamp, Price, Volume, beg, end)
			respchan <- resp
		default:
			time.Sleep(time.Duration(rand.Intn(200))*time.Millisecond)
		}
		
		end = ( (end + 1) % buflen )
		if end == beg {
			beg = ( (beg + 1) % buflen )
		}

	}
}


func genResp(req *Request, Tstamp []int64, Price []float64, Volume []int64, beg, end int) *Response {

	indexmap := []int{ beg }
	buflen   := len(Tstamp)
	ii       := ((beg + 1)% buflen)
	endp1    := ((end + 1)% buflen)
	for ii != endp1 {
		indexmap = append(indexmap, ii)
		ii = ( (ii+1) % buflen )
	}

	//fmt.Println("indexmap len =", len(indexmap) ,"indexmap : ", indexmap, "req :", req)

	low  := 0
	high := len(indexmap) - 1
	startloc := -1
	for low <= high {
		mid := ((low + high)/2)
		if Tstamp[indexmap[mid]] <= req.From {
			if mid + 1 > high {
				startloc = mid;
				break
			}
			if Tstamp[indexmap[mid+1]] > req.From {
				startloc = mid + 1
				break
			} else {
				low = mid
			}
		} else {
			if mid - 1 < low {
				startloc = mid
				break
			}
			if Tstamp[indexmap[mid-1]] <= req.From {
				startloc = mid
				break
			} else {
				high = mid
			}
		}
	}

	if startloc == -1 {
		return &Response{}
	}

	resplen := len(indexmap) - startloc
	t := make([]int64, resplen)
	p := make([]float64, resplen)
	v := make([]int64, resplen)

	jj := 0
	for ii := startloc; ii < len(indexmap); ii++ {
		t[jj] = Tstamp[indexmap[ii]]
		p[jj] = Price[indexmap[ii]]
		v[jj] = Volume[indexmap[ii]]
		jj += 1
	}

	return &Response{Tstamp: t, Price: p, Volume: v}
}
