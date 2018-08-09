from app import api, jwt
from flask_restful import Resource, reqparse, abort, request

spch_parser = reqparse.RequestParser()
spch_parser.add_argument('voice_id', help='voice id', location='json', required=True)
spch_parser.add_argument('streaming', choices=['true','false'], \
    help='true|false, default=false', location='json')
spch_parser.add_argument('audio_format', choices=['wav|ogg|mp3'], \
    help='wav|ogg|mp3, default=wav', location='json')
spch_parser.add_argument('text', help='text to syntesise speech', location='json', required=True)


class Speech(Resource):
    def post(self):
        """ Speech endpoint
        
            Args:
                voice_id (str)
                streaming (boolean, optional)
                audio_format (boolean, optional)
                text (str)
                
            Returns: 
                audio file of the speech
        """
        return {'message':'Not implemented yet'}, 501


api.add_resource(Speech, '/speech')
