package main

import (
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"io"
	"log"
	"net"
	"os"
	"sync"
)

const (
	MaxPacketSize   = 100_000_000
	DefaultQueueLen = 1024*1024
	ListenAddr      = "0.0.0.0:1935"
	HeaderSize      = 28
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

type State struct {
	ID           string
	Clients      map[*Client]bool
	VideoCodecID int
	AudioCodecID int
	FPS          int
	Width        int
	Height       int
	VideoExtra   []byte
	AudioExtra   []byte
	Mu           sync.RWMutex
	Queue        chan Packet
}

type Server struct {
	Streams map[string]*State
	Mu      sync.RWMutex
}

type Client struct {
	Conn     net.Conn
	FoundKeyFrame bool
	StreamID string
}

func NewServer() *Server {
	return &Server{
		Streams: make(map[string]*State),
		Mu:      sync.RWMutex{},
	}
}

func NewState(id string) *State {
	return &State{
		ID:      id,
		Clients: make(map[*Client]bool),
		Queue:   make(chan Packet, DefaultQueueLen),
	}
}

func main() {
	server := NewServer()

	ln, err := net.Listen("tcp", ListenAddr)
	if err != nil {
		panic(err)
	}
	defer ln.Close()

	log.Println("Server listening on", ListenAddr)
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
	streamID, ok := header["stream_id"].(string)
	if !ok {
		log.Println("Invalid input, missing stream_id")
		return
	}

	videoCodecID, ok := header["video_codec_id"].(float64)
	if !ok {
		log.Println("Invalid input, missing video_codec_id")
		return
	}

	audioCodecID, ok := header["audio_codec_id"].(float64)
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

	file, err := os.Create(streamID)
	if err != nil {
		log.Println("ERROR: cannot create file:", err)
		return
	}
	defer file.Close()

	server.Mu.Lock()
	state, ok := server.Streams[streamID]
	if !ok {
		state = NewState(streamID)
		server.Streams[streamID] = state
		go server.publisher(state)
	}
	state.VideoCodecID = int(videoCodecID)
	state.AudioCodecID = int(audioCodecID)
	state.FPS = int(fps)
	state.Width = int(width)
	state.Height = int(height)
	state.VideoExtra = videoExtraData
	state.AudioExtra = audioExtraData
	server.Mu.Unlock()

	log.Printf(
		"Push client connected, streamID=%s, videoCodecID=%d, audioCodecID=%d, fps=%d, video_extradata=%d bytes, audio_extradata=%d bytes",
		streamID, int(videoCodecID), int(audioCodecID), int(fps), len(videoExtraData), len(audioExtraData),
	)

	for {
		bytes := make([]byte, HeaderSize)
		if _, err := io.ReadFull(conn, bytes); err != nil {
			log.Printf("Push client disconnected, streamID: %s", streamID)
			break
		}

		h := Header{
			Pts:         int64(binary.LittleEndian.Uint64(bytes[0:8])),
			Dts:         int64(binary.LittleEndian.Uint64(bytes[8:16])),
			StreamIndex: int32(binary.LittleEndian.Uint32(bytes[16:20])),
			Flags:       int32(binary.LittleEndian.Uint32(bytes[20:24])),
			Size:        int32(binary.LittleEndian.Uint32(bytes[24:28])),
		}

		if h.Size < 0 || h.Size > MaxPacketSize {
			log.Printf("Invalid packet size: %d, disconnecting client %s", h.Size, conn.RemoteAddr().String())
			break
		}

		payload := make([]byte, h.Size)
		if _, err := io.ReadFull(conn, payload); err != nil {
			log.Printf("Push client disconnected, streamID: %s", streamID)
			break
		}

		packet := Packet{Header: h, Payload: payload}
		logPacket(packet)

		if packet.Header.StreamIndex == 0 {
			if _, err := file.Write(packet.Payload); err != nil {
				log.Println("ERROR: cannot write packet:", err)
			}
		}

		state.Mu.RLock()
		select {
		case state.Queue <- packet:
		default:
			log.Printf("Dropping packet for stream %s, queue full", streamID)
		}
		state.Mu.RUnlock()
	}
}

func handlePull(conn net.Conn, header map[string]any, server *Server) {
	streamID, ok := header["stream_id"].(string)
	if !ok {
		log.Println("Invalid input, missing stream_id")
		return
	}

	server.Mu.RLock()
	state, ok := server.Streams[streamID]
	server.Mu.RUnlock()
	if !ok {
		log.Println("Pull client requested unknown stream:", streamID)
		return
	}

	resp := map[string]any{
		"stream_id":      streamID,
		"video_codec_id": state.VideoCodecID,
		"audio_codec_id": state.AudioCodecID,
		"fps":            state.FPS,
		"height":         state.Height,
		"width":          state.Width,
	}

	if len(state.VideoExtra) > 0 {
		resp["video_extradata"] = base64.StdEncoding.EncodeToString(state.VideoExtra)
	}

	if len(state.AudioExtra) > 0 {
		resp["audio_extradata"] = base64.StdEncoding.EncodeToString(state.AudioExtra)
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

	log.Printf("Pull client connected, streamID=%s, videoCodecID=%d, audioCodecID=%d, fps=%d, video_extradata=%d bytes, audio_extradata=%d bytes",
		streamID, state.VideoCodecID, state.AudioCodecID, state.FPS, len(state.VideoExtra), len(state.AudioExtra))

	client := &Client{Conn: conn, FoundKeyFrame: false, StreamID: streamID}
	server.addClient(client)
	defer server.removeClient(client, streamID)

	buf := make([]byte, 1)
	for {
		if _, err := conn.Read(buf); err != nil {
			log.Printf("Pull client disconnected, streamID: %s", streamID)
			return
		}
	}
}

func (server *Server) addClient(client *Client) {
	server.Mu.Lock()
	state, ok := server.Streams[client.StreamID]
	if !ok {
		state = NewState(client.StreamID)
		server.Streams[client.StreamID] = state
		go server.publisher(state)
	}
	state.Mu.Lock()
	state.Clients[client] = true
	state.Mu.Unlock()
	server.Mu.Unlock()
}

func (server *Server) removeClient(client *Client, streamID string) {
	server.Mu.Lock()
	state, ok := server.Streams[streamID]
	if ok {
		state.Mu.Lock()
		delete(state.Clients, client)
		state.Mu.Unlock()
	}
	server.Mu.Unlock()
	client.Conn.Close()
}

func (server *Server) publishPacket(pkt Packet, client *Client) error {
	header := make([]byte, HeaderSize)
	binary.LittleEndian.PutUint64(header[0:8], uint64(pkt.Header.Pts))
	binary.LittleEndian.PutUint64(header[8:16], uint64(pkt.Header.Dts))
	binary.LittleEndian.PutUint32(header[16:20], uint32(pkt.Header.StreamIndex))
	binary.LittleEndian.PutUint32(header[20:24], uint32(pkt.Header.Flags))
	binary.LittleEndian.PutUint32(header[24:28], uint32(pkt.Header.Size))

	if _, err := client.Conn.Write(header); err != nil {
		return err
	}
	if _, err := client.Conn.Write(pkt.Payload); err != nil {
		return err
	}
	return nil
}

func (server *Server) publisher(state *State) {
	for pkt := range state.Queue {
		state.Mu.RLock()
		for client := range state.Clients {
			if pkt.Header.StreamIndex == 0 && !client.FoundKeyFrame {
				if pkt.Header.Flags&1 == 0 {
					continue
				}
				client.FoundKeyFrame = true
			}

			if err := server.publishPacket(pkt, client); err != nil {
				log.Printf("Failed to send packet to client: %v", err)
			}
		}
		state.Mu.RUnlock()
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
