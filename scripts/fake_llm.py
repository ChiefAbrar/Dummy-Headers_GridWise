#!/usr/bin/env python3
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        n = int(self.headers.get('Content-Length', '0'))
        body = json.loads(self.rfile.read(n))
        user = body['messages'][1]['content']
        req = json.loads(user)
        notes = req['operator_notes']
        out = []
        for item in notes:
            text = item['operator_note'].lower()
            i = item['note_index']
            if 'wash' in text and '25%' in text:
                d = {'note_index': i, 'applies': True, 'directive_type': 'solar_reduction',
                     'structured_adjustment': {'hours': [12, 13], 'factor': 0.25},
                     'explanation': 'Test LLM: reduced solar during washing.'}
            else:
                d = {'note_index': i, 'applies': False, 'directive_type': 'no_op',
                     'structured_adjustment': None,
                     'explanation': 'Test LLM: unrelated note.'}
            out.append(d)
        response = {'choices': [{'message': {'content': json.dumps({'interpretations': out})}}]}
        data = json.dumps(response).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)
    def log_message(self, *_):
        pass

ThreadingHTTPServer(('127.0.0.1', 19090), Handler).serve_forever()
