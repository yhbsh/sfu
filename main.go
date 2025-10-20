package main

import (
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"io"
	"log"
	"net"
	"sync"
)

type Header struct {
	Pts         int64
	Dts         int64
	StreamIndex int32
	Flags       int32
	Size        int32
}

type Packet struct {
	Header  Header
	Payload []byte
}

type Server struct {
	clients        map[string]map[*Client]bool
	videoCodecs    map[string]int
	audioCodecs    map[string]int
	fps            map[string]int
	//accessUnits    map[string][]Packet
	widths         map[string]int
	heights        map[string]int
	queues         map[string]chan Packet
	videoExtraData map[string][]byte
	audioExtraData map[string][]byte
	mu             *sync.RWMutex
}

type Client struct {
	conn     net.Conn
	keyframe bool
	streamId string
}

func main() {
	server := &Server{
		clients:        make(map[string]map[*Client]bool),
		videoCodecs:    make(map[string]int),
		audioCodecs:    make(map[string]int),
		fps:            make(map[string]int),
		//accessUnits:    make(map[string][]Packet),
		widths:         make(map[string]int),
		heights:        make(map[string]int),
		queues:         make(map[string]chan Packet),
		videoExtraData: make(map[string][]byte),
		audioExtraData: make(map[string][]byte),
		mu:             &sync.RWMutex{},
	}

	ln, err := net.Listen("tcp", "0.0.0.0:1935")
	if err != nil {
		panic(err)
	}
	defer ln.Close()

	log.Println("Server listening on 0.0.0.0:1935")
	for {
		conn, err := ln.Accept()
		if err != nil {
			continue
		}
		go handleConnection(conn, server)
	}
}

func handleConnection(conn net.Conn, server *Server) {
	defer conn.Close()

	var length uint32
	if err := binary.Read(conn, binary.LittleEndian, &length); err != nil {
		return
	}

	payload := make([]byte, length)
	if _, err := io.ReadFull(conn, payload); err != nil {
		return
	}

	var header map[string]any
	if err := json.Unmarshal(payload, &header); err != nil {
		log.Println("Invalid JSON header:", err)
		return
	}

	mode, ok := header["mode"].(string)
	if !ok {
		log.Println("Invalid input, missing mode")
		return
	}

	switch mode {
	case "push":
		handlePush(conn, header, server)
	case "pull":
		handlePull(conn, header, server)
	default:
		log.Printf("Invalid input, mode %s is not supported", mode)
	}
}

func handlePush(conn net.Conn, header map[string]any, server *Server) {
	streamId, ok := header["stream_id"].(string)
	if !ok {
		log.Println("Invalid input, missing stream_id")
		return
	}

	videoCodecId, ok := header["video_codec_id"].(float64)
	if !ok {
		log.Println("Invalid input, missing video_codec_id")
		return
	}

	audioCodecId, ok := header["audio_codec_id"].(float64)
	if !ok {
		log.Println("Invalid input, missing audio_codec_id")
		return
	}

	fps, ok := header["fps"].(float64)
	if !ok {
		log.Println("Invalid input, missing fps")
		return
	}

	width, ok := header["width"].(float64)
	if !ok {
		log.Println("Invalid input, missing width")
		return
	}

	height, ok := header["height"].(float64)
	if !ok {
		log.Println("Invalid input, missing height")
		return
	}

	var videoExtraData []byte
	if encoded, ok := header["video_extradata"].(string); ok && encoded != "" {
		var err error
		videoExtraData, err = base64.StdEncoding.DecodeString(encoded)
		if err != nil {
			log.Println("Failed to decode video_extradata:", err)
			return
		}
	}

	var audioExtraData []byte
	if encoded, ok := header["audio_extradata"].(string); ok && encoded != "" {
		var err error
		audioExtraData, err = base64.StdEncoding.DecodeString(encoded)
		if err != nil {
			log.Println("Failed to decode audio_extradata:", err)
			return
		}
	}

	server.mu.Lock()
	server.videoCodecs[streamId] = int(videoCodecId)
	server.audioCodecs[streamId] = int(audioCodecId)
	server.videoExtraData[streamId] = videoExtraData
	server.audioExtraData[streamId] = audioExtraData
	server.widths[streamId] = int(width)
	server.heights[streamId] = int(height)
	server.fps[streamId] = int(fps)

	if server.queues[streamId] == nil {
		server.queues[streamId] = make(chan Packet)
		go server.publisher(streamId)
	}

	server.mu.Unlock()

	log.Printf("Push client connected, streamId=%s, videoCodecId=%d, audioCodecId=%d, fps=%d, video_extradata=%d bytes, audio_extradata=%d bytes",
		streamId, int(videoCodecId), int(audioCodecId), int(fps), len(videoExtraData), len(audioExtraData))

	for {
		bytes := make([]byte, 28)
		if _, err := io.ReadFull(conn, bytes); err != nil {
			log.Printf("Push client disconnected, streamId: %s", streamId)
			break
		}

		header := Header{
			Pts:         int64(binary.LittleEndian.Uint64(bytes[0:8])),
			Dts:         int64(binary.LittleEndian.Uint64(bytes[8:16])),
			StreamIndex: int32(binary.LittleEndian.Uint32(bytes[16:20])),
			Flags:       int32(binary.LittleEndian.Uint32(bytes[20:24])),
			Size:        int32(binary.LittleEndian.Uint32(bytes[24:28])),
		}

		if header.Size < 0 || header.Size > 100_000_000 {
			log.Printf("Invalid packet size: %d, disconnecting client %s", header.Size, conn.RemoteAddr().String())
			break
		}

		payload := make([]byte, header.Size)
		if _, err := io.ReadFull(conn, payload); err != nil {
			log.Printf("Push client disconnected, streamId: %s", streamId)
			break
		}

		packet := Packet{Header: header, Payload: payload}

		//server.mu.Lock()
		//if header.StreamIndex == 0 && header.Flags&1 != 0 {
		//	server.accessUnits[streamId] = []Packet{packet}
		//} else {
		//	if au, ok := server.accessUnits[streamId]; ok && len(au) > 0 {
		//		server.accessUnits[streamId] = append(au, packet)
		//	}
		//}
		//server.mu.Unlock()

		server.mu.RLock()
		queue := server.queues[streamId]
		server.mu.RUnlock()
		queue <- packet
	}
}

func handlePull(conn net.Conn, header map[string]any, server *Server) {
	streamId, ok := header["stream_id"].(string)
	if !ok {
		log.Println("Invalid input, missing stream_id")
		return
	}

	server.mu.RLock()
	videoCodecId, ok := server.videoCodecs[streamId]
	if !ok {
		log.Println("Pull client requested unknown stream:", streamId)
		server.mu.RUnlock()
		return
	}

	audioCodecId, ok := server.audioCodecs[streamId]
	if !ok {
		log.Println("Pull client requested unknown stream:", streamId)
		server.mu.RUnlock()
		return
	}

	fps, ok := server.fps[streamId]
	if !ok {
		log.Println("Pull client requested unknown stream:", streamId)
		server.mu.RUnlock()
		return
	}

	width, ok := server.widths[streamId]
	if !ok {
		log.Println("Pull client requested unknown stream:", streamId)
		server.mu.RUnlock()
		return
	}

	height, ok := server.heights[streamId]
	if !ok {
		log.Println("Pull client requested unknown stream:", streamId)
		server.mu.RUnlock()
		return
	}

	videoExtraData := server.videoExtraData[streamId]
	audioExtraData := server.audioExtraData[streamId]
	//accessUnit, hasAU := server.accessUnits[streamId]
	server.mu.RUnlock()

	resp := map[string]any{
		"stream_id":      streamId,
		"video_codec_id": videoCodecId,
		"audio_codec_id": audioCodecId,
		"fps":            fps,
		"height":         height,
		"width":          width,
	}

	if len(videoExtraData) > 0 {
		resp["video_extradata"] = base64.StdEncoding.EncodeToString(videoExtraData)
	}

	if len(audioExtraData) > 0 {
		resp["audio_extradata"] = base64.StdEncoding.EncodeToString(audioExtraData)
	}

	data, err := json.Marshal(resp)
	if err != nil {
		log.Println("Failed to marshal pull response:", err)
		return
	}

	length := uint32(len(data))
	if err := binary.Write(conn, binary.LittleEndian, length); err != nil {
		log.Println("Failed to write pull response length:", err)
		return
	}

	if _, err := conn.Write(data); err != nil {
		log.Println("Failed to write pull response data:", err)
		return
	}

	log.Printf("Pull client connected, streamId=%s, videoCodecId=%d, audioCodecId=%d, fps=%d, video_extradata=%d bytes, audio_extradata=%d bytes",
		streamId, videoCodecId, audioCodecId, fps, len(videoExtraData), len(audioExtraData))

	//if hasAU && len(accessUnit) > 0 {
	//	for _, pkt := range accessUnit {
	//		headerBytes := make([]byte, 28)
	//		binary.LittleEndian.PutUint64(headerBytes[0:8], uint64(pkt.Header.Pts))
	//		binary.LittleEndian.PutUint64(headerBytes[8:16], uint64(pkt.Header.Dts))
	//		binary.LittleEndian.PutUint32(headerBytes[16:20], uint32(pkt.Header.StreamIndex))
	//		binary.LittleEndian.PutUint32(headerBytes[20:24], uint32(pkt.Header.Flags))
	//		binary.LittleEndian.PutUint32(headerBytes[24:28], uint32(pkt.Header.Size))

	//		if _, err := conn.Write(headerBytes); err != nil {
	//			log.Println("Failed to send AU header to pull client:", err)
	//			return
	//		}

	//		if _, err := conn.Write(pkt.Payload); err != nil {
	//			log.Println("Failed to send AU payload to pull client:", err)
	//			return
	//		}
	//	}
	//	log.Printf("Sent initial access unit to pull client, streamId=%s, packets=%d", streamId, len(accessUnit))
	//} else {
	//	log.Printf("No access unit available yet for streamId=%s", streamId)
	//}

	client := &Client{conn: conn, keyframe: false, streamId: streamId}
	server.addClient(client)
	defer server.removeClient(client, streamId)

	buf := make([]byte, 1)
	for {
		if _, err := conn.Read(buf); err != nil {
			log.Printf("Pull client disconnected, streamId: %s, codecId: %d", streamId, videoCodecId)
			return
		}
	}
}

func (server *Server) addClient(client *Client) {
	server.mu.Lock()
	if server.clients[client.streamId] == nil {
		server.clients[client.streamId] = make(map[*Client]bool)
	}
	server.clients[client.streamId][client] = true
	server.mu.Unlock()
}

func (server *Server) removeClient(client *Client, streamId string) {
	server.mu.Lock()
	delete(server.clients[streamId], client)
	if len(server.clients[streamId]) == 0 {
		delete(server.clients, streamId)
	}
	server.mu.Unlock()
	client.conn.Close()
}

func (server *Server) publisher(streamId string) {
	for pkt := range server.queues[streamId] {
		logPacket(pkt)

		server.mu.RLock()
		clients := server.clients[streamId]
		for client := range clients {
			if pkt.Header.StreamIndex == 0 && !client.keyframe {
				if pkt.Header.Flags & 1 == 0 {
					continue
				}

				client.keyframe = true
			}

			header := make([]byte, 28)
			binary.LittleEndian.PutUint64(header[0:8], uint64(pkt.Header.Pts))
			binary.LittleEndian.PutUint64(header[8:16], uint64(pkt.Header.Dts))
			binary.LittleEndian.PutUint32(header[16:20], uint32(pkt.Header.StreamIndex))
			binary.LittleEndian.PutUint32(header[20:24], uint32(pkt.Header.Flags))
			binary.LittleEndian.PutUint32(header[24:28], uint32(pkt.Header.Size))

			client.conn.Write(header)
			client.conn.Write(pkt.Payload)
		}
		server.mu.RUnlock()
	}
}

func logPacket(packet Packet) {
	log.Printf("Packet: Pts=%d Dts=%d StreamIndex=%d Flags=%d Size=%d",
		packet.Header.Pts,
		packet.Header.Dts,
		packet.Header.StreamIndex,
		packet.Header.Flags,
		packet.Header.Size)
}
